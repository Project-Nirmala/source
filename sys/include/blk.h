#ifndef _NIRMALA_BLK_H
#define _NIRMALA_BLK_H

#include <lemonfs.h>

struct blkdrv {
    const char *name;
    uint32_t    nblocks;
    void       *priv;
    int       (*read)(void *priv, uint32_t blk, void *buf);
    int       (*write)(void *priv, uint32_t blk, const void *buf);
};

struct blkdev *blkcache_create(struct blkdrv *drv, unsigned nbuf);

struct blkdrv *virtio_blk_probe(void);
struct blkdrv *virtio_blk_attach(uint16_t iobase);

#endif
