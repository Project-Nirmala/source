#include <blk.h>
#include <mm.h>
#include <kern.h>

struct cbuf {
    uint32_t blk;
    uint32_t holds;
    uint64_t stamp;
    bool     valid;
    uint8_t *data;
};

struct cache {
    struct blkdrv *drv;
    struct cbuf   *bufs;
    unsigned       nbuf;
    uint64_t       clock;
};

static struct cbuf *find(struct cache *c, uint32_t blk)
{
    unsigned i;

    for (i = 0; i < c->nbuf; i++)
        if (c->bufs[i].valid && c->bufs[i].blk == blk)
            return &c->bufs[i];
    return NULL;
}

static uint8_t *cache_get(struct blkdev *dev, uint32_t blk)
{
    struct cache *c = dev->priv;
    struct cbuf *b = find(c, blk), *victim = NULL;
    unsigned i;

    if (blk >= c->drv->nblocks)
        panic("blkcache: block %u beyond end of device", blk);
    if (b) {
        b->holds++;
        b->stamp = ++c->clock;
        return b->data;
    }
    for (i = 0; i < c->nbuf; i++) {
        struct cbuf *x = &c->bufs[i];

        if (x->holds)
            continue;
        if (!x->valid) {
            victim = x;
            break;
        }
        if (!victim || x->stamp < victim->stamp)
            victim = x;
    }
    if (!victim)
        panic("blkcache: every buffer is pinned");
    victim->valid = false;
    if (c->drv->read(c->drv->priv, blk, victim->data) < 0)
        panic("%s: read error on block %u", c->drv->name, blk);
    victim->blk = blk;
    victim->valid = true;
    victim->holds = 1;
    victim->stamp = ++c->clock;
    return victim->data;
}

static void cache_put(struct blkdev *dev, uint32_t blk, int dirty)
{
    struct cache *c = dev->priv;
    struct cbuf *b = find(c, blk);

    if (!b || b->holds == 0)
        panic("blkcache: put of block %u that is not held", blk);
    b->holds--;
    if (dirty && c->drv->write(c->drv->priv, blk, b->data) < 0)
        kprintf("%s: write error on block %u\n", c->drv->name, blk);
}

struct blkdev *blkcache_create(struct blkdrv *drv, unsigned nbuf)
{
    struct blkdev *dev = kzalloc(sizeof(*dev));
    struct cache *c = kzalloc(sizeof(*c));
    uint8_t *mem = kzalloc((size_t)nbuf * LEMON_BSIZE);
    unsigned i;

    if (!dev || !c || !mem)
        return NULL;
    c->drv = drv;
    c->nbuf = nbuf;
    c->bufs = kzalloc(sizeof(struct cbuf) * nbuf);
    if (!c->bufs)
        return NULL;
    for (i = 0; i < nbuf; i++)
        c->bufs[i].data = mem + (size_t)i * LEMON_BSIZE;
    dev->nblocks = drv->nblocks;
    dev->priv = c;
    dev->get = cache_get;
    dev->put = cache_put;
    return dev;
}
