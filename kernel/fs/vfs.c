#include "vfs.h"
#include "../../kernel/mm/heap.h"
#include "../../include/version.h"
#include "../../lib/string.h"
#include "../../lib/kprintf.h"

vnode_t *vfs_root = NULL;
file_t   fd_table[VFS_MAX_FDS];

/* ── vnode helpers ─────────────────────────────────────────── */

vnode_t *vnode_create(vnode_t *parent, const char *name,
                      vnode_type_t type, vfs_ops_t *ops) {
    vnode_t *vn = (vnode_t *)kzalloc(sizeof(vnode_t));
    if (!vn) return NULL;

    strncpy(vn->name, name, VFS_NAME_MAX - 1);
    vn->type   = type;
    vn->ops    = ops;
    vn->parent = parent;

    if (parent) {
        vn->next_sib    = parent->children;
        parent->children = vn;
    }
    return vn;
}

/* ── Path resolution ───────────────────────────────────────── */

vnode_t *vfs_lookup(const char *path) {
    if (!path || path[0] != '/') return NULL;

    vnode_t *cur = vfs_root;
    if (path[1] == '\0') return cur;   /* root itself */

    char buf[VFS_MAX_PATH];
    strncpy(buf, path + 1, sizeof(buf) - 1);

    char *tok = buf;
    while (tok && *tok) {
        char *slash = strchr(tok, '/');
        if (slash) *slash = '\0';

        vnode_t *child = cur->children;
        while (child) {
            if (strcmp(child->name, tok) == 0) { cur = child; goto next; }
            child = child->next_sib;
        }
        /* ops->lookup fallback */
        if (cur->ops && cur->ops->lookup) {
            cur = cur->ops->lookup(cur, tok);
            if (!cur) return NULL;
        } else {
            return NULL;
        }
    next:
        tok = slash ? slash + 1 : NULL;
    }
    return cur;
}

/* ── FD table ──────────────────────────────────────────────── */

int vfs_open(const char *path) {
    vnode_t *vn = vfs_lookup(path);
    if (!vn) return -1;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!fd_table[i].valid) {
            fd_table[i].vnode  = vn;
            fd_table[i].offset = 0;
            fd_table[i].valid  = true;
            return i;
        }
    }
    return -1;
}

void vfs_close(int fd) {
    if (fd >= 0 && fd < VFS_MAX_FDS) fd_table[fd].valid = false;
}

int vfs_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].valid) return -1;
    file_t *f  = &fd_table[fd];
    vnode_t *vn = f->vnode;
    if (!vn->ops || !vn->ops->read) return -1;
    int r = vn->ops->read(vn, buf, count, f->offset);
    if (r > 0) f->offset += (size_t)r;
    return r;
}

int vfs_write(int fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].valid) return -1;
    file_t *f  = &fd_table[fd];
    vnode_t *vn = f->vnode;
    if (!vn->ops || !vn->ops->write) return -1;
    int r = vn->ops->write(vn, buf, count, f->offset);
    if (r > 0) f->offset += (size_t)r;
    return r;
}

int vfs_readdir(const char *path, uint32_t idx,
                char *name_out, vnode_type_t *type_out) {
    vnode_t *vn = vfs_lookup(path);
    if (!vn || vn->type != VFS_DIR) return -1;
    if (vn->ops && vn->ops->readdir)
        return vn->ops->readdir(vn, idx, name_out, type_out);
    /* Fallback: walk children linked list */
    vnode_t *c = vn->children;
    for (uint32_t i = 0; c; i++, c = c->next_sib) {
        if (i == idx) {
            strncpy(name_out, c->name, VFS_NAME_MAX - 1);
            *type_out = c->type;
            return 0;
        }
    }
    return -1;
}

/* ── tmpfs ops ─────────────────────────────────────────────── */

/* tmpfs file data: a plain byte buffer. */
typedef struct {
    uint8_t *data;
    size_t   size;
    size_t   cap;
} tmpfs_file_t;

static int tmpfs_read(vnode_t *vn, void *buf, size_t count, size_t off) {
    tmpfs_file_t *tf = (tmpfs_file_t *)vn->data;
    if (!tf || off >= tf->size) return 0;
    size_t avail = tf->size - off;
    size_t n     = count < avail ? count : avail;
    memcpy(buf, tf->data + off, n);
    return (int)n;
}

static int tmpfs_write(vnode_t *vn, const void *buf, size_t count, size_t off) {
    tmpfs_file_t *tf = (tmpfs_file_t *)vn->data;
    if (!tf) {
        tf = (tmpfs_file_t *)kzalloc(sizeof(tmpfs_file_t));
        if (!tf) return -1;
        vn->data = tf;
    }
    size_t need = off + count;
    if (need > tf->cap) {
        size_t new_cap = need * 2 + 64;
        uint8_t *nb = (uint8_t *)krealloc(tf->data, new_cap);
        if (!nb) return -1;
        tf->data = nb;
        tf->cap  = new_cap;
    }
    memcpy(tf->data + off, buf, count);
    if (off + count > tf->size) tf->size = off + count;
    vn->size = tf->size;
    return (int)count;
}

static vfs_ops_t tmpfs_file_ops = { tmpfs_read, tmpfs_write, NULL, NULL };
static vfs_ops_t tmpfs_dir_ops  = { NULL, NULL, NULL, NULL };

/* ── devfs: /dev/tty, /dev/null, /dev/serial ──────────────── */

static int dev_null_read (vnode_t *v __attribute__((unused)),
                          void *b __attribute__((unused)),
                          size_t c __attribute__((unused)),
                          size_t o __attribute__((unused))) { return 0; }
static int dev_null_write(vnode_t *v __attribute__((unused)),
                          const void *b __attribute__((unused)),
                          size_t c, size_t o __attribute__((unused))) {
    return (int)c; }
static vfs_ops_t dev_null_ops  = { dev_null_read,  dev_null_write, NULL, NULL };

/* /dev/tty: keyboard input, VGA output — wired by the shell directly. */
static vfs_ops_t dev_tty_ops = { NULL, NULL, NULL, NULL }; /* shell uses kbd/vga directly */

/* ── vfs_init ──────────────────────────────────────────────── */

void vfs_init(void) {
    memset(fd_table, 0, sizeof(fd_table));

    /* Root directory (tmpfs). */
    vfs_root = (vnode_t *)kzalloc(sizeof(vnode_t));
    strncpy(vfs_root->name, "/", VFS_NAME_MAX - 1);
    vfs_root->type = VFS_DIR;
    vfs_root->ops  = &tmpfs_dir_ops;

    /* /dev */
    vnode_t *dev = vnode_create(vfs_root, "dev", VFS_DIR, &tmpfs_dir_ops);

    vnode_create(dev, "null",   VFS_DEVICE, &dev_null_ops);
    vnode_create(dev, "zero",   VFS_DEVICE, &dev_null_ops);   /* reads 0s */
    vnode_create(dev, "tty",    VFS_DEVICE, &dev_tty_ops);

    /* /etc */
    vnode_t *etc  = vnode_create(vfs_root, "etc",  VFS_DIR,  &tmpfs_dir_ops);

    /* /etc/hostname */
    vnode_t *host = vnode_create(etc,  "hostname",  VFS_FILE, &tmpfs_file_ops);
    const char *hostname = "lithium\n";
    tmpfs_write(host, hostname, strlen(hostname), 0);

    /* /etc/motd */
    vnode_t *motd = vnode_create(etc,  "motd",      VFS_FILE, &tmpfs_file_ops);
    const char *motd_txt =
        "Welcome to lithium OS, kernel ver- " LITHIUM_VERSION ".\n"
        "Type 'help' for a list of commands.\n";
    tmpfs_write(motd, motd_txt, strlen(motd_txt), 0);

    /* /proc */
    vnode_create(vfs_root, "proc", VFS_DIR, &tmpfs_dir_ops);

    kprintf("VFS: tmpfs root mounted, /dev populated\n");
}
