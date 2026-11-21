#include <blk.h>
#include <kern.h>
#include <sys/errno.h>

#define SECTOR 512u
#define BULK_SECTORS 128u

static struct blkdrv *devs[BLK_MAX];
static unsigned       ndevs;
static struct blkdrv *root_drv;
static uint64_t       root_start, root_nsect;

void blk_register(struct blkdrv *d)
{
    unsigned i;

    for (i = 0; i < ndevs; i++)
        if (devs[i] == d)
            return;
    if (ndevs == BLK_MAX)
        return;
    if (!d->nsectors)
        d->nsectors = (uint64_t)d->nblocks * (LEMON_BSIZE / SECTOR);
    if (!d->model[0])
        memcpy(d->model, "generic block device", 21);
    devs[ndevs++] = d;
}

unsigned blk_count(void)
{
    return ndevs;
}

struct blkdrv *blk_get(unsigned i)
{
    return i < ndevs ? devs[i] : NULL;
}

void blk_set_root(struct blkdrv *d, uint64_t start, uint64_t nsect)
{
    root_drv = d;
    root_start = start;
    root_nsect = nsect;
}

bool blk_overlaps_root(const struct blkdrv *d, uint64_t start, uint64_t nsect)
{
    if (!d || d != root_drv)
        return false;
    return start < root_start + root_nsect && root_start < start + nsect;
}

static int sect_rw(struct blkdrv *d, uint64_t lba, uint32_t count, void *buf, int write)
{
    uint8_t tmp[LEMON_BSIZE];
    uint8_t *p = buf;

    if (d->rw)
        return d->rw(d->priv, lba, count, buf, write);
    while (count) {
        uint32_t blk = (uint32_t)(lba / (LEMON_BSIZE / SECTOR));
        uint32_t off = (uint32_t)(lba % (LEMON_BSIZE / SECTOR)) * SECTOR;

        if (!off && count >= LEMON_BSIZE / SECTOR) {
            int r = write ? d->write(d->priv, blk, p) : d->read(d->priv, blk, p);

            if (r < 0)
                return -EIO;
            lba += LEMON_BSIZE / SECTOR;
            count -= LEMON_BSIZE / SECTOR;
            p += LEMON_BSIZE;
            continue;
        }
        if (d->read(d->priv, blk, tmp) < 0)
            return -EIO;
        if (write) {
            memcpy(tmp + off, p, SECTOR);
            if (d->write(d->priv, blk, tmp) < 0)
                return -EIO;
        } else {
            memcpy(p, tmp + off, SECTOR);
        }
        lba++;
        count--;
        p += SECTOR;
    }
    return 0;
}

int blk_rw_bytes(struct blkdrv *d, uint64_t off, void *buf, size_t n, int write)
{
    uint8_t tmp[SECTOR];
    uint8_t *p = buf;

    if (off + n > d->nsectors * SECTOR)
        return -EINVAL;
    while (n) {
        uint64_t lba = off / SECTOR;
        uint32_t so = (uint32_t)(off % SECTOR);

        if (so || n < SECTOR) {
            size_t c = SECTOR - so;

            if (c > n)
                c = n;
            if (sect_rw(d, lba, 1, tmp, 0) < 0)
                return -EIO;
            if (write) {
                memcpy(tmp + so, p, c);
                if (sect_rw(d, lba, 1, tmp, 1) < 0)
                    return -EIO;
            } else {
                memcpy(p, tmp + so, c);
            }
            off += c;
            p += c;
            n -= c;
        } else {
            uint32_t cnt = (uint32_t)(n / SECTOR);

            if (cnt > BULK_SECTORS)
                cnt = BULK_SECTORS;
            if (sect_rw(d, lba, cnt, p, write) < 0)
                return -EIO;
            off += (uint64_t)cnt * SECTOR;
            p += (size_t)cnt * SECTOR;
            n -= (size_t)cnt * SECTOR;
        }
    }
    return 0;
}

int blk_flush(struct blkdrv *d)
{
    return d->flush ? d->flush(d->priv) : 0;
}

int blk_sync_all(void)
{
    unsigned i;
    int r = 0;

    for (i = 0; i < ndevs; i++)
        if (blk_flush(devs[i]) < 0)
            r = -EIO;
    return r;
}
