#pragma once

#include "../../include/types.h"

/* Maximum open files per process (for Phase 15 shell). */
#define VFS_MAX_FDS  32
#define VFS_MAX_PATH 256
#define VFS_NAME_MAX 64

typedef enum {
    VFS_FILE    = 0,
    VFS_DIR     = 1,
    VFS_DEVICE  = 2,
    VFS_SYMLINK = 3,
} vnode_type_t;

struct vnode;
typedef struct vnode vnode_t;

typedef struct vfs_ops {
    int    (*read) (vnode_t *vn, void *buf, size_t count, size_t off);
    int    (*write)(vnode_t *vn, const void *buf, size_t count, size_t off);
    vnode_t *(*lookup)(vnode_t *dir, const char *name);
    int    (*readdir)(vnode_t *dir, uint32_t idx, char *name_out, vnode_type_t *type_out);
} vfs_ops_t;

struct vnode {
    char          name[VFS_NAME_MAX];
    vnode_type_t  type;
    uint64_t      size;
    void         *data;       /* filesystem-private data */
    vfs_ops_t    *ops;
    vnode_t      *parent;
    vnode_t      *children;   /* linked list */
    vnode_t      *next_sib;   /* sibling linkage */
};

/* File descriptor. */
typedef struct {
    vnode_t *vnode;
    size_t   offset;
    bool     valid;
} file_t;

/* VFS globals. */
extern vnode_t *vfs_root;

/* Initialise: create root tmpfs, mount devfs at /dev. */
void vfs_init(void);

/* Resolve a path from the root. Returns NULL on failure. */
vnode_t *vfs_lookup(const char *path);

/* Open a file; returns fd ≥ 0 or -1 on failure. */
int   vfs_open (const char *path);
void  vfs_close(int fd);
int   vfs_read (int fd, void *buf, size_t count);
int   vfs_write(int fd, const void *buf, size_t count);
int   vfs_open_in (file_t files[VFS_MAX_FDS], const char *path);
void  vfs_close_in(file_t files[VFS_MAX_FDS], int fd);
int   vfs_read_in (file_t files[VFS_MAX_FDS], int fd, void *buf, size_t count);
int   vfs_write_in(file_t files[VFS_MAX_FDS], int fd, const void *buf, size_t count);
int   vfs_readdir(const char *path, uint32_t idx,
                  char *name_out, vnode_type_t *type_out);
int   vfs_mkdir(const char *path);
int   vfs_create_file(const char *path, const void *data, size_t size);
int   vfs_read_all(const char *path, void **out_buf, size_t *out_size);

/* Create a new vnode under a parent directory. */
vnode_t *vnode_create(vnode_t *parent, const char *name,
                      vnode_type_t type, vfs_ops_t *ops);
