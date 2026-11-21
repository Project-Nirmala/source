#ifndef _NIRMALA_BLK_H
#define _NIRMALA_BLK_H

#include <stdbool.h>
#include <lemonfs.h>

#define BLK_MAX 16

struct vnode;

struct blkdrv {
    const char *name;
    uint32_t    nblocks;
    void       *priv;
    int       (*read)(void *priv, uint32_t blk, void *buf);
    int       (*write)(void *priv, uint32_t blk, const void *buf);
    uint64_t    nsectors;
    char        model[48];
    int       (*rw)(void *priv, uint64_t lba, uint32_t count, void *buf, int write);
    int       (*flush)(void *priv);
};

struct blkdev *blkcache_create(struct blkdrv *drv, unsigned nbuf);

struct blkdrv *virtio_blk_probe(void);
struct blkdrv *virtio_blk_attach(uint16_t iobase);
struct blkdrv *ahci_probe(void);

void           blk_register(struct blkdrv *d);
unsigned       blk_count(void);
struct blkdrv *blk_get(unsigned i);
void           blk_set_root(struct blkdrv *d, uint64_t start, uint64_t nsect);
bool           blk_overlaps_root(const struct blkdrv *d, uint64_t start, uint64_t nsect);
int            blk_rw_bytes(struct blkdrv *d, uint64_t off, void *buf, size_t n, int write);
int            blk_flush(struct blkdrv *d);
int            blk_sync_all(void);

void           devfs_init(void);
void           devfs_attach(struct vnode *root);
void           devfs_add_blob(const char *name, const void *data, size_t size);
bool           devfs_resolve_root(const char *spec, struct blkdrv **parent, uint64_t *start, uint64_t *nsect,
                                  const char **name);
struct blkdrv *devfs_part_drv(struct blkdrv *parent, uint64_t start, uint64_t nsect, const char *name);

#endif
