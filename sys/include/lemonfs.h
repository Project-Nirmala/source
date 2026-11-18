#ifndef _NIRMALA_LEMONFS_H
#define _NIRMALA_LEMONFS_H

#include <stdint.h>
#include <stddef.h>

#define LEMON_BSIZE    4096u
#define LEMON_ISIZE    128u
#define LEMON_IPB      (LEMON_BSIZE / LEMON_ISIZE)
#define LEMON_NDIRECT  12u
#define LEMON_PPB      (LEMON_BSIZE / 4u)
#define LEMON_DSIZE    64u
#define LEMON_DPB      (LEMON_BSIZE / LEMON_DSIZE)
#define LEMON_NAME_MAX 58u
#define LEMON_ROOT_INO 1u

#define LEMON_IFMT  0170000u
#define LEMON_IFDIR 0040000u
#define LEMON_IFREG 0100000u

#define LEMON_DT_DIR 4
#define LEMON_DT_REG 8

struct lemon_sb {
    char     magic[8];
    uint32_t version;
    uint32_t bsize;
    uint32_t nblocks;
    uint32_t ninodes;
    uint32_t ibmap_blk, ibmap_n;
    uint32_t bbmap_blk, bbmap_n;
    uint32_t itab_blk, itab_n;
    uint32_t data_blk;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t root_ino;
    char     label[16];
};

struct lemon_inode {
    uint16_t mode;
    uint16_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint32_t mtime;
    uint32_t ctime;
    uint32_t direct[LEMON_NDIRECT];
    uint32_t indirect;
    uint32_t dindirect;
    uint8_t  pad[40];
};

struct lemon_dirent {
    uint32_t ino;
    uint8_t  namelen;
    uint8_t  type;
    char     name[LEMON_NAME_MAX];
};

struct blkdev {
    uint32_t  nblocks;
    void     *priv;
    uint8_t *(*get)(struct blkdev *dev, uint32_t blk);
    void    (*put)(struct blkdev *dev, uint32_t blk, int dirty);
};

struct lemon {
    struct blkdev  *dev;
    struct lemon_sb sb;
    uint32_t        bhint, ihint;
    uint32_t      (*now)(void);
    void          (*log)(const char *fmt, ...);
};

int  lemon_mkfs(struct blkdev *dev, uint32_t ninodes, const char *label);
int  lemon_mount(struct lemon *l, struct blkdev *dev);

int  lemon_iget(struct lemon *l, uint32_t ino, struct lemon_inode *ip);
int  lemon_iput(struct lemon *l, uint32_t ino, const struct lemon_inode *ip);
int  lemon_ialloc(struct lemon *l, uint16_t mode, uint32_t *ino);
int  lemon_ifree(struct lemon *l, uint32_t ino);

long lemon_read(struct lemon *l, uint32_t ino, uint64_t off, void *buf, size_t n);
long lemon_write(struct lemon *l, uint32_t ino, uint64_t off, const void *buf, size_t n);
int  lemon_truncate(struct lemon *l, uint32_t ino, uint64_t size);
int  lemon_set_mtime(struct lemon *l, uint32_t ino, uint32_t mtime);

int  lemon_dir_lookup(struct lemon *l, uint32_t dino, const char *name, uint32_t *ino, int *type);
int  lemon_dir_add(struct lemon *l, uint32_t dino, const char *name, uint32_t ino, int type);
int  lemon_dir_remove(struct lemon *l, uint32_t dino, const char *name);
int  lemon_dir_read(struct lemon *l, uint32_t dino, uint64_t idx, char *name, uint32_t *ino, int *type);
int  lemon_dir_empty(struct lemon *l, uint32_t dino);

struct blkdev *ramdisk_create(uint32_t nblocks);

int  lemon_check(struct lemon *l, uint8_t *scratch);

#endif
