#ifndef _NIRMALA_VFS_H
#define _NIRMALA_VFS_H

#include <types.h>

struct file;
struct blkdev;

#define NAME_MAX 255
#define PATH_MAX 1024

enum vtype { VREG = 1, VDIR, VCHR, VPIPE, VBLK };

struct vnode;

struct vnops {
    long (*read)(struct vnode *vn, uint64_t off, void *buf, size_t n);
    long (*write)(struct vnode *vn, uint64_t off, const void *buf, size_t n);
    long (*ioctl)(struct vnode *vn, uint64_t req, uint64_t arg);
    void (*release)(struct vnode *vn);
};

struct vstatfs {
    uint64_t bsize, blocks, bfree, files, ffree;
    uint32_t namelen;
};

struct fsops {
    int (*lookup)(struct vnode *dir, const char *name, struct vnode **out);
    int (*create)(struct vnode *dir, const char *name, uint32_t mode, int isdir, struct vnode **out);
    int (*remove)(struct vnode *dir, const char *name, int want_dir);
    int (*rename)(struct vnode *odir, const char *oname, struct vnode *ndir, const char *nname);
    int (*readdir)(struct vnode *dir, uint64_t idx, char *name, struct vnode **ent);
    int (*truncate)(struct vnode *vn, uint64_t size);
    int (*statfs)(struct vnode *vn, struct vstatfs *out);
    int (*settime)(struct vnode *vn, uint32_t mtime);
};

struct vnode {
    enum vtype          type;
    const struct vnops *ops;
    const struct fsops *fs;
    uint64_t            ino;
    uint64_t            size;
    void               *priv;
    struct vnode       *parent;
    uint32_t            mode;
    uint32_t            nlink;
    uint32_t            mtime;
    int                 refs;
    char                name[NAME_MAX + 1];
};

extern struct vnode console_vnode;

void          vfs_init(const void *image, size_t size);
struct vnode *vfs_root(void);
struct vnode *devfs_root(void);
int           vfs_lookup(const char *path, struct vnode *cwd, struct vnode **out);
int           vfs_create(const char *path, struct vnode *cwd, uint32_t mode, int excl, struct vnode **out);
int           vfs_mkdir(const char *path, struct vnode *cwd, uint32_t mode);
int           vfs_unlink(const char *path, struct vnode *cwd);
int           vfs_rmdir(const char *path, struct vnode *cwd);
int           vfs_rename(const char *from, const char *to, struct vnode *cwd);
int           vfs_truncate(struct vnode *vn, uint64_t size);
int           vfs_statfs(struct vnode *vn, struct vstatfs *out);
int           vfs_utime(struct vnode *vn, uint32_t mtime);
long          vnode_read(struct vnode *vn, uint64_t off, void *buf, size_t n);
long          vnode_write(struct vnode *vn, uint64_t off, const void *buf, size_t n);
long          vnode_ioctl(struct vnode *vn, uint64_t req, uint64_t arg);
int           vfs_readdir(struct vnode *dir, uint64_t idx, struct vnode **ent, char *name);
long          vfs_getpath(struct vnode *vn, char *buf, size_t size);
uint64_t      vfs_next_ino(void);
int           pipe_create(struct file **rd, struct file **wr);
void          tarfs_extract(const void *image, size_t size);
struct vnode *lemonfs_mount(struct blkdev *dev);
int           lemonfs_probe(struct blkdev *dev);
int           lemonfs_check(void);

#endif
