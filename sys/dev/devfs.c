#include <vfs.h>
#include <blk.h>
#include <kern.h>
#include <uaccess.h>
#include <sys/errno.h>
#include <sys/blkio.h>

#define MAX_NODES 48
#define MAX_PARTS 16

struct devnode {
    struct vnode   vn;
    struct blkdrv *drv;
    uint64_t       start, nsect;
    bool           used, whole;
};

static struct devnode nodes[MAX_NODES];
static struct vnode   dev_dir;

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
    return le32(p) | (uint64_t)le32(p + 4) << 32;
}

static long dev_read(struct vnode *vn, uint64_t off, void *buf, size_t n)
{
    struct devnode *d = vn->priv;
    uint64_t size = d->nsect * 512;

    if (off >= size)
        return 0;
    if (n > size - off)
        n = (size_t)(size - off);
    if (blk_rw_bytes(d->drv, d->start * 512 + off, buf, n, 0) < 0)
        return -EIO;
    return (long)n;
}

static long dev_write(struct vnode *vn, uint64_t off, const void *buf, size_t n)
{
    struct devnode *d = vn->priv;
    uint64_t size = d->nsect * 512;

    if (blk_is_root(d->drv))
        return -EBUSY;
    if (off >= size)
        return -ENOSPC;
    if (n > size - off)
        n = (size_t)(size - off);
    if (blk_rw_bytes(d->drv, d->start * 512 + off, (void *)buf, n, 1) < 0)
        return -EIO;
    return (long)n;
}

static void scan_disk(struct blkdrv *drv);

static long dev_ioctl(struct vnode *vn, uint64_t req, uint64_t arg)
{
    struct devnode *d = vn->priv;
    uint64_t v64;
    int v;

    switch (req) {
    case BLKGETSIZE64:
        v64 = d->nsect * 512;
        return copy_to_user(arg, &v64, sizeof(v64)) < 0 ? -EFAULT : 0;
    case BLKSSZGET:
        v = 512;
        return copy_to_user(arg, &v, sizeof(v)) < 0 ? -EFAULT : 0;
    case BLKFLSBUF:
        return blk_flush(d->drv);
    case BLKRRPART:
        if (!d->whole)
            return -EINVAL;
        scan_disk(d->drv);
        return 0;
    case NIR_BLKMODEL:
        return copy_to_user(arg, d->drv->model, strlen(d->drv->model) + 1) < 0 ? -EFAULT : 0;
    case NIR_BLKSTART:
        v64 = d->start;
        return copy_to_user(arg, &v64, sizeof(v64)) < 0 ? -EFAULT : 0;
    case NIR_BLKROOT:
        v = blk_is_root(d->drv);
        return copy_to_user(arg, &v, sizeof(v)) < 0 ? -EFAULT : 0;
    }
    return -ENOTTY;
}

static const struct vnops dev_ops = {
    .read = dev_read,
    .write = dev_write,
    .ioctl = dev_ioctl,
};

static struct devnode *node_add(const char *name, struct blkdrv *drv, uint64_t start, uint64_t nsect, bool whole)
{
    unsigned i;

    for (i = 0; i < MAX_NODES; i++) {
        struct devnode *d = &nodes[i];

        if (d->used)
            continue;
        memset(d, 0, sizeof(*d));
        d->used = true;
        d->whole = whole;
        d->drv = drv;
        d->start = start;
        d->nsect = nsect;
        d->vn.type = VBLK;
        d->vn.ops = &dev_ops;
        d->vn.ino = vfs_next_ino();
        d->vn.size = nsect * 512;
        d->vn.priv = d;
        d->vn.parent = &dev_dir;
        d->vn.mode = 060660;
        d->vn.nlink = 1;
        memcpy(d->vn.name, name, strlen(name) + 1);
        return d;
    }
    return NULL;
}

static void part_add(struct blkdrv *drv, unsigned num, uint64_t first, uint64_t count)
{
    char name[24];
    size_t len = strlen(drv->name);
    unsigned k = 0;
    char digits[4];

    if (first >= drv->nsectors || count == 0 || first + count > drv->nsectors)
        return;
    memcpy(name, drv->name, len);
    while (num) {
        digits[k++] = (char)('0' + num % 10);
        num /= 10;
    }
    while (k)
        name[len++] = digits[--k];
    name[len] = 0;
    node_add(name, drv, first, count, false);
}

static void scan_gpt(struct blkdrv *drv)
{
    uint8_t hdr[512], ent[512];
    uint64_t ent_lba;
    uint32_t nent, esz, i, z;

    if (blk_rw_bytes(drv, 512, hdr, 512, 0) < 0 || memcmp(hdr, "EFI PART", 8))
        return;
    ent_lba = le64(hdr + 72);
    nent = le32(hdr + 80);
    esz = le32(hdr + 84);
    if (esz < 128 || esz > 512 || nent == 0)
        return;
    if (nent > MAX_PARTS)
        nent = MAX_PARTS;
    for (i = 0; i < nent; i++) {
        uint64_t first, last;

        if (blk_rw_bytes(drv, ent_lba * 512 + (uint64_t)i * esz, ent, esz, 0) < 0)
            return;
        for (z = 0; z < 16 && !ent[z]; z++)
            ;
        if (z == 16)
            continue;
        first = le64(ent + 32);
        last = le64(ent + 40);
        if (last < first)
            continue;
        part_add(drv, i + 1, first, last - first + 1);
    }
}

static void scan_mbr(struct blkdrv *drv, const uint8_t *s)
{
    unsigned i;

    for (i = 0; i < 4; i++) {
        const uint8_t *e = s + 446 + i * 16;

        if (e[4] == 0 || e[4] == 0x05 || e[4] == 0x0F || e[4] == 0x85)
            continue;
        part_add(drv, i + 1, le32(e + 8), le32(e + 12));
    }
}

static void scan_disk(struct blkdrv *drv)
{
    uint8_t s[512];
    unsigned i;

    for (i = 0; i < MAX_NODES; i++)
        if (nodes[i].used && !nodes[i].whole && nodes[i].drv == drv)
            nodes[i].used = false;
    if (blk_rw_bytes(drv, 0, s, 512, 0) < 0 || s[510] != 0x55 || s[511] != 0xAA)
        return;
    if (s[450] == 0xEE)
        scan_gpt(drv);
    else
        scan_mbr(drv, s);
}

static int dev_lookup(struct vnode *dir, const char *name, struct vnode **out)
{
    unsigned i;

    (void)dir;
    for (i = 0; i < MAX_NODES; i++)
        if (nodes[i].used && !strcmp(nodes[i].vn.name, name)) {
            *out = &nodes[i].vn;
            return 0;
        }
    return -ENOENT;
}

static int dev_readdir(struct vnode *dir, uint64_t idx, char *name, struct vnode **ent)
{
    unsigned i;

    (void)dir;
    for (i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].used)
            continue;
        if (idx-- == 0) {
            memcpy(name, nodes[i].vn.name, strlen(nodes[i].vn.name) + 1);
            *ent = &nodes[i].vn;
            return 1;
        }
    }
    return 0;
}

static int dev_create(struct vnode *dir, const char *name, uint32_t mode, int isdir, struct vnode **out)
{
    (void)dir;
    (void)name;
    (void)mode;
    (void)isdir;
    (void)out;
    return -EROFS;
}

static int dev_remove(struct vnode *dir, const char *name, int want_dir)
{
    (void)dir;
    (void)name;
    (void)want_dir;
    return -EROFS;
}

static int dev_rename(struct vnode *od, const char *on, struct vnode *nd, const char *nn)
{
    (void)od;
    (void)on;
    (void)nd;
    (void)nn;
    return -EROFS;
}

static const struct fsops dev_fsops = {
    .lookup = dev_lookup,
    .create = dev_create,
    .remove = dev_remove,
    .rename = dev_rename,
    .readdir = dev_readdir,
};

struct vnode *devfs_root(void)
{
    return &dev_dir;
}

void devfs_init(void)
{
    unsigned i;

    memset(&dev_dir, 0, sizeof(dev_dir));
    dev_dir.type = VDIR;
    dev_dir.fs = &dev_fsops;
    dev_dir.ino = vfs_next_ino();
    dev_dir.parent = vfs_root();
    dev_dir.mode = 040755;
    dev_dir.nlink = 2;
    memcpy(dev_dir.name, "dev", 4);
    for (i = 0; i < blk_count(); i++) {
        struct blkdrv *drv = blk_get(i);

        node_add(drv->name, drv, 0, drv->nsectors, true);
        scan_disk(drv);
    }
}
