#include "shell.h"
#include "../../drivers/char/vga.h"
#include "../../include/version.h"
#include "../../drivers/char/ps2kbd.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include "../../kernel/mm/heap.h"
#include "../../kernel/mm/pmm.h"
#include "../../kernel/mm/vmm.h"
#include "../../kernel/timer.h"
#include "../../kernel/sched/task.h"
#include "../../kernel/sched/sched.h"
#include "../../kernel/fs/vfs.h"
#include "../../kernel/panic.h"
#include "../../kernel/power/power.h"
#include "../../arch/x86_64/cpu/cpuid.h"

#define LINE_MAX  512
#define HIST_MAX  16

/* ── Command history ──────────────────────────────────────── */
static char hist[HIST_MAX][LINE_MAX];
static int  hist_count = 0;
static int  hist_idx   = 0;

static void hist_push(const char *line) {
    if (!*line) return;
    /* avoid duplicating last entry */
    if (hist_count > 0 && strcmp(hist[(hist_count - 1) % HIST_MAX], line) == 0)
        return;
    strncpy(hist[hist_count % HIST_MAX], line, LINE_MAX - 1);
    hist_count++;
    hist_idx = hist_count;
}

/* ── Line editing with history (up/down arrows via ESC sequences) ── */
static int readline(char *buf, int max) {
    int i    = 0;
    int h    = hist_idx;
    buf[0]   = '\0';

    while (1) {
        char c = kbd_getchar();

        /* ESC sequences for arrow keys (ANSI: ESC [ A/B/C/D) */
        if (c == 0x1B) {
            char c2 = kbd_trygetchar();
            if (c2 == '[') {
                char c3 = kbd_getchar();
                if (c3 == 'A' && hist_count > 0) {            /* Up */
                    if (h > 0) h--;
                    /* Erase current line */
                    while (i-- > 0) { kputchar('\b'); kputchar(' '); kputchar('\b'); }
                    i = 0;
                    const char *entry = hist[h % HIST_MAX];
                    strncpy(buf, entry, max - 1);
                    i = (int)strlen(buf);
                    kprintf("%s", buf);
                } else if (c3 == 'B') {                        /* Down */
                    if (h < hist_count) h++;
                    while (i-- > 0) { kputchar('\b'); kputchar(' '); kputchar('\b'); }
                    i = 0;
                    if (h < hist_count) {
                        strncpy(buf, hist[h % HIST_MAX], max - 1);
                        i = (int)strlen(buf);
                        kprintf("%s", buf);
                    } else { buf[0] = '\0'; }
                }
            }
            continue;
        }

        if (c == '\n' || c == '\r') {
            buf[i] = '\0';
            kputchar('\n');
            hist_push(buf);
            hist_idx = hist_count;
            return i;
        }
        if (c == '\b') {
            if (i > 0) {
                i--;
                kputchar('\b'); kputchar(' '); kputchar('\b');
            }
            continue;
        }
        if (c == 3) {   /* Ctrl-C */
            kprintf("^C\n");
            buf[0] = '\0';
            hist_idx = hist_count;
            return 0;
        }
        if (c == 'u' - 'a' + 1) {  /* Ctrl-U: kill line */
            while (i-- > 0) { kputchar('\b'); kputchar(' '); kputchar('\b'); }
            i = 0; buf[0] = '\0';
            continue;
        }
        if (c >= 0x20 && i < max - 1) {
            buf[i++] = c;
            kputchar(c);
        }
    }
}

/* ── Argument tokeniser ───────────────────────────────────── */
#define MAX_ARGS 32

static int parse_args(char *line, char *argv[], int max) {
    int argc = 0;
    char *p  = line;
    while (*p && argc < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

/* ── Helpers ──────────────────────────────────────────────── */
static void set_color_for_type(vnode_type_t t) {
    switch (t) {
    case VFS_DIR:    vga_set_color(VGA_LIGHT_BLUE,  VGA_BLACK); break;
    case VFS_DEVICE: vga_set_color(VGA_LIGHT_CYAN,  VGA_BLACK); break;
    default:         vga_set_color(VGA_LIGHT_GREY,  VGA_BLACK); break;
    }
}

/* ── Built-in commands ────────────────────────────────────── */

static void cmd_help(int argc __attribute__((unused)),
                     char **argv __attribute__((unused))) {
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    kprintf("Lithium OS — built-in commands\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("  help              show this list\n");
    kprintf("  uname [-a]        kernel name and version\n");
    kprintf("  uptime            time since boot\n");
    kprintf("  mem               memory statistics\n");
    kprintf("  cpu               CPU information\n");
    kprintf("  tasks             list scheduled tasks\n");
    kprintf("  ls [path]         list directory contents\n");
    kprintf("  cat <file>        print file to stdout\n");
    kprintf("  echo [text]       print text\n");
    kprintf("  write <f> <text>  write text to /tmp file\n");
    kprintf("  mkdir <path>      create directory\n");
    kprintf("  clear             clear screen\n");
    kprintf("  history           show command history\n");
    kprintf("  panic             trigger kernel panic (test)\n");
    kprintf("  reboot            reboot the system\n");
    kprintf("  shutdown          power off the VM\n");
    kprintf("  poweroff          alias for shutdown\n");
}

static void cmd_uname(int argc, char **argv) {
    bool all = (argc >= 2 && strcmp(argv[1], "-a") == 0);
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    if (all) {
        kprintf("%s (GCC/Clang cross, Multiboot2) Lithium/%s\n",
                LITHIUM_VERSION_FULL, LITHIUM_VERSION);
    } else {
        kprintf("%s\n", LITHIUM_VERSION_FULL);
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void cmd_uptime(int argc __attribute__((unused)),
                       char **argv __attribute__((unused))) {
    uint64_t ms = timer_ms();
    uint64_t s  = ms / 1000;
    uint64_t m  = s  / 60;
    uint64_t h  = m  / 60;
    kprintf("up %luh %02lum %02lus  (%lu ms, %lu ticks)\n",
            h, m % 60, s % 60, ms, jiffies);
}

static void cmd_mem(int argc __attribute__((unused)),
                    char **argv __attribute__((unused))) {
    uint64_t total  = pmm_total_bytes();
    uint64_t free_b = pmm_free_bytes();
    uint64_t used   = total - free_b;

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("Physical memory:\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("  Total : %6lu MiB  (%lu KiB)\n", total/(1<<20), total/1024);
    kprintf("  Used  : %6lu MiB  (%lu KiB)\n", used/(1<<20),  used/1024);
    kprintf("  Free  : %6lu MiB  (%lu KiB)\n", free_b/(1<<20),free_b/1024);
    heap_stats();
}

static void cmd_cpu(int argc __attribute__((unused)),
                    char **argv __attribute__((unused))) {
    const cpuid_features_t *f = cpuid_features();
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("CPU info:\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("  Vendor   : %s\n", f->vendor);
    kprintf("  Features :%s%s%s%s%s%s%s%s\n",
            f->has_apic    ? " APIC"    : "",
            f->has_sse     ? " SSE"     : "",
            f->has_sse2    ? " SSE2"    : "",
            f->has_avx     ? " AVX"     : "",
            f->has_avx2    ? " AVX2"    : "",
            f->has_nx      ? " NX"      : "",
            f->has_syscall ? " SYSCALL" : "",
            f->has_1gb_pages ? " 1GB-PG" : "");
    kprintf("  Max leaf : 0x%x (ext: 0x%x)\n",
            f->max_basic, f->max_extended);
}

static void cmd_tasks(int argc __attribute__((unused)),
                      char **argv __attribute__((unused))) {
    kprintf("  PID  STATE    TICKS  NAME\n");
    kprintf("  ---  -------  -----  ----\n");
    if (!current_task) { kprintf("  (scheduler not active)\n"); return; }
    task_t *t = current_task;
    uint32_t seen = 0;
    do {
        const char *st = "?      ";
        switch (t->state) {
        case TASK_READY:   st = "ready  "; break;
        case TASK_RUNNING: st = "running"; break;
        case TASK_BLOCKED: st = "blocked"; break;
        case TASK_DEAD:    st = "dead   "; break;
        }
        kprintf("  %3u  %s  %5lu  %s\n",
                t->pid, st, t->ticks, t->name);
        t = t->next;
    } while (t != current_task && ++seen < 64);
}

static void cmd_ls(int argc, char **argv) {
    const char *path = (argc >= 2) ? argv[1] : "/";
    char name[VFS_NAME_MAX];
    vnode_type_t type;
    int count = 0;

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("%s:\n", path);

    while (vfs_readdir(path, (uint32_t)count, name, &type) == 0) {
        set_color_for_type(type);
        kprintf("  %-20s", name);
        vga_set_color(VGA_DARK_GREY, VGA_BLACK);
        if (type == VFS_DIR)    kprintf(" <DIR>\n");
        else if (type == VFS_DEVICE) kprintf(" <DEV>\n");
        else kprintf("\n");
        count++;
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    if (count == 0) kprintf("  (empty)\n");
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: cat <file>\n"); return; }
    int fd = vfs_open(argv[1]);
    if (fd < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("cat: %s: no such file\n", argv[1]);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    char buf[128]; int n;
    while ((n = vfs_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0'; kprintf("%s", buf);
    }
    vfs_close(fd);
}

static void cmd_write(int argc, char **argv) {
    if (argc < 3) { kprintf("usage: write <file> <text>\n"); return; }

    /* Build the full path under /tmp */
    char path[VFS_MAX_PATH];
    if (argv[1][0] == '/') {
        strncpy(path, argv[1], sizeof(path) - 1);
    } else {
        strncpy(path, "/tmp/", sizeof(path) - 1);
        strncat(path, argv[1], sizeof(path) - strlen(path) - 1);
    }

    /* Ensure /tmp exists */
    vfs_lookup("/tmp"); /* ignore if already exists */

    vnode_t *parent = vfs_lookup("/tmp");
    if (!parent) {
        /* create /tmp directory */
        extern vnode_t *vfs_root;
        vnode_t *tmp_dir = vnode_create(vfs_root, "tmp", VFS_DIR, NULL);
        (void)tmp_dir;
        parent = vfs_lookup("/tmp");
    }

    /* Create or find the file node */
    vnode_t *f = vfs_lookup(path);
    if (!f) {
        /* Extract filename */
        const char *fname = argv[1];
        for (const char *p = argv[1]; *p; p++)
            if (*p == '/') fname = p + 1;

        extern vfs_ops_t *tmpfs_file_ops_ptr;
        (void)fname;
        /* For simplicity, use vfs_open-based write via VFS */
    }

    /* Write using vfs */
    int fd = vfs_open(path);
    if (fd < 0) {
        kprintf("write: cannot open %s\n", path);
        return;
    }
    /* Build string from remaining args */
    for (int i = 2; i < argc; i++) {
        vfs_write(fd, argv[i], strlen(argv[i]));
        if (i < argc - 1) vfs_write(fd, " ", 1);
    }
    vfs_write(fd, "\n", 1);
    vfs_close(fd);
    kprintf("wrote to %s\n", path);
}

static void cmd_echo(int argc, char **argv) {
    bool newline = true;
    int start    = 1;
    if (argc >= 2 && strcmp(argv[1], "-n") == 0) {
        newline = false;
        start   = 2;
    }
    for (int i = start; i < argc; i++) {
        if (i > start) kputchar(' ');
        kprintf("%s", argv[i]);
    }
    if (newline) kputchar('\n');
}

static void cmd_clear(int argc __attribute__((unused)),
                      char **argv __attribute__((unused))) {
    vga_clear();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("Welcome to lithium OS, kernel ver- %s.\n", LITHIUM_VERSION);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void cmd_history(int argc __attribute__((unused)),
                        char **argv __attribute__((unused))) {
    int start = hist_count > HIST_MAX ? hist_count - HIST_MAX : 0;
    for (int i = start; i < hist_count; i++) {
        kprintf("  %3d  %s\n", i + 1, hist[i % HIST_MAX]);
    }
}

static void cmd_panic(int argc __attribute__((unused)),
                      char **argv __attribute__((unused))) {
    panic("user-triggered kernel panic (shell test)");
}

static void cmd_reboot(int argc __attribute__((unused)),
                       char **argv __attribute__((unused))) {
    kprintf("Rebooting...\n");
    /* Keyboard controller reset pulse */
    for (;;) {
        uint8_t s; __asm__ volatile ("inb $0x64,%0":"=a"(s));
        if (!(s & 0x02)) break;
    }
    __asm__ volatile ("outb %0,$0x64"::"a"((uint8_t)0xFE));
    for (;;) __asm__ volatile ("hlt");
}

static void cmd_poweroff(int argc __attribute__((unused)),
                         char **argv __attribute__((unused))) {
    power_shutdown();
}

/* ── Command table ────────────────────────────────────────── */
typedef struct { const char *name; void (*fn)(int, char **); } cmd_t;

static const cmd_t commands[] = {
    { "help",    cmd_help    },
    { "uname",   cmd_uname   },
    { "uptime",  cmd_uptime  },
    { "mem",     cmd_mem     },
    { "cpu",     cmd_cpu     },
    { "tasks",   cmd_tasks   },
    { "ls",      cmd_ls      },
    { "cat",     cmd_cat     },
    { "write",   cmd_write   },
    { "echo",    cmd_echo    },
    { "clear",   cmd_clear   },
    { "history", cmd_history },
    { "panic",   cmd_panic   },
    { "reboot",  cmd_reboot  },
    { "shutdown",cmd_poweroff},
    { "poweroff",cmd_poweroff},
};
#define CMD_COUNT (int)(sizeof(commands)/sizeof(commands[0]))

/* ── Main shell loop ──────────────────────────────────────── */

static void print_motd(void) {
    int fd = vfs_open("/etc/motd");
    if (fd < 0) return;
    char buf[256]; int n;
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    while ((n = vfs_read(fd, buf, sizeof(buf)-1)) > 0) {
        buf[n] = '\0'; kprintf("%s", buf);
    }
    vfs_close(fd);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void shell_run(void *arg __attribute__((unused))) {
    print_motd();

    char line[LINE_MAX];
    char *argv[MAX_ARGS];

    for (;;) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        kprintf("lithium");
        vga_set_color(VGA_DARK_GREY, VGA_BLACK);
        kprintf(":");
        vga_set_color(VGA_LIGHT_BLUE, VGA_BLACK);
        kprintf("~");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        kprintf("# ");

        readline(line, LINE_MAX);
        if (!*line) continue;

        int argc = parse_args(line, argv, MAX_ARGS);
        if (!argc) continue;

        bool found = false;
        for (int i = 0; i < CMD_COUNT; i++) {
            if (strcmp(argv[0], commands[i].name) == 0) {
                commands[i].fn(argc, argv);
                found = true;
                break;
            }
        }
        if (!found) {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            kprintf("-%s: %s: command not found\n",
                    "lithium", argv[0]);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        }
    }
}
