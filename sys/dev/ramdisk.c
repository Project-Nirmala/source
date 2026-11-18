#include <lemonfs.h>
#include <mm.h>
#include <kern.h>

static uint8_t *rd_get(struct blkdev *dev, uint32_t blk)
{
    return (uint8_t *)dev->priv + (size_t)blk * LEMON_BSIZE;
}

static void rd_put(struct blkdev *dev, uint32_t blk, int dirty)
{
    (void)dev;
    (void)blk;
    (void)dirty;
}

struct blkdev *ramdisk_create(uint32_t nblocks)
{
    struct blkdev *dev = kzalloc(sizeof(*dev));

    if (!dev)
        return NULL;
    dev->priv = kzalloc((size_t)nblocks * LEMON_BSIZE);
    if (!dev->priv) {
        kfree(dev);
        return NULL;
    }
    dev->nblocks = nblocks;
    dev->get = rd_get;
    dev->put = rd_put;
    return dev;
}
