#include <vfs.h>
#include <blk.h>
#include <kern.h>
#include <uaccess.h>
#include <sys/errno.h>
#include <sys/blkio.h>
#include <cmdline.h>
#include <swap.h>
#include <mouse.h>
#include <fbcon.h>

#define MAX_NODES 48
#define MAX_PARTS 16
#define MAX_CHR   6

struct devnode {
    struct vnode   vn;
    struct blkdrv *drv;
    uint64_t       start, nsect;
    bool           used, whole;
    char           label[37];
    char           uuid[37];
};

struct memfile {
    struct vnode vn;
    const void  *data;
};

struct partdrv {
    struct blkdrv  drv;
    struct blkdrv *parent;
    uint64_t       start;
    char           name[24];
    bool           used;
};

static struct devnode nodes[MAX_NODES];
static struct memfile memfiles[4];
static struct partdrv partdrvs[2];
static struct vnode   dev_dir;
static struct vnode  *chrdevs[MAX_CHR];

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

    if (blk_overlaps_root(d->drv, d->start, d->nsect) || swap_overlaps(d->drv, d->start, d->nsect))
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
        v = blk_overlaps_root(d->drv, d->start, d->nsect);
        return copy_to_user(arg, &v, sizeof(v)) < 0 ? -EFAULT : 0;
    case NIR_BLKSWAP:
        v = swap_overlaps(d->drv, d->start, d->nsect);
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

static struct devnode *part_add(struct blkdrv *drv, unsigned num, uint64_t first, uint64_t count)
{
    char name[24];
    size_t len = strlen(drv->name);
    unsigned k = 0;
    char digits[4];

    if (first >= drv->nsectors || count == 0 || first + count > drv->nsectors)
        return NULL;
    memcpy(name, drv->name, len);
    if (len && drv->name[len - 1] >= '0' && drv->name[len - 1] <= '9')
        name[len++] = 'p';
    while (num) {
        digits[k++] = (char)('0' + num % 10);
        num /= 10;
    }
    while (k)
        name[len++] = digits[--k];
    name[len] = 0;
    return node_add(name, drv, first, count, false);
}

static void gpt_uuid_str(const uint8_t *g, char *out)
{
    static const int order[16] = { 3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15 };
    static const char hex[] = "0123456789abcdef";
    unsigned i, o = 0;

    for (i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out[o++] = '-';
        out[o++] = hex[g[order[i]] >> 4];
        out[o++] = hex[g[order[i]] & 15];
    }
    out[o] = 0;
}

static void scan_gpt(struct blkdrv *drv)
{
    uint8_t hdr[512], ent[512];
    uint64_t ent_lba;
    uint32_t nent, esz, i, z;
    struct devnode *d;

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
        d = part_add(drv, i + 1, first, last - first + 1);
        if (d) {
            gpt_uuid_str(ent + 16, d->uuid);
            for (z = 0; z < 36 && ent[56 + 2 * z]; z++)
                d->label[z] = (char)ent[56 + 2 * z];
            d->label[z] = 0;
        }
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
    for (i = 0; i < 4; i++)
        if (memfiles[i].data && !strcmp(memfiles[i].vn.name, name)) {
            *out = &memfiles[i].vn;
            return 0;
        }
    for (i = 0; i < MAX_CHR; i++)
        if (chrdevs[i] && !strcmp(chrdevs[i]->name, name)) {
            *out = chrdevs[i];
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
    for (i = 0; i < 4; i++) {
        if (!memfiles[i].data)
            continue;
        if (idx-- == 0) {
            memcpy(name, memfiles[i].vn.name, strlen(memfiles[i].vn.name) + 1);
            *ent = &memfiles[i].vn;
            return 1;
        }
    }
    for (i = 0; i < MAX_CHR; i++) {
        if (!chrdevs[i])
            continue;
        if (idx-- == 0) {
            memcpy(name, chrdevs[i]->name, strlen(chrdevs[i]->name) + 1);
            *ent = chrdevs[i];
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

void devfs_add_chr(struct vnode *vn)
{
    unsigned i;

    for (i = 0; i < MAX_CHR; i++)
        if (!chrdevs[i]) {
            if (!vn->ino)
                vn->ino = vfs_next_ino();
            if (!vn->nlink)
                vn->nlink = 1;
            vn->parent = &dev_dir;
            chrdevs[i] = vn;
            return;
        }
}

void devfs_init(void)
{
    unsigned i;

    memset(&dev_dir, 0, sizeof(dev_dir));
    dev_dir.type = VDIR;
    dev_dir.fs = &dev_fsops;
    dev_dir.ino = vfs_next_ino();
    dev_dir.mode = 040755;
    dev_dir.nlink = 2;
    memcpy(dev_dir.name, "dev", 4);
    for (i = 0; i < blk_count(); i++) {
        struct blkdrv *drv = blk_get(i);

        node_add(drv->name, drv, 0, drv->nsectors, true);
        scan_disk(drv);
    }
    devfs_add_chr(&console_vnode);
    if (mouse_present())
        devfs_add_chr(&mouse_vnode);
    if (fbcon_text_ready())
        devfs_add_chr(&fbtext_vnode);
}

void devfs_attach(struct vnode *root)
{
    unsigned i;

    dev_dir.parent = root;
    for (i = 0; i < 4; i++)
        if (memfiles[i].data)
            memfiles[i].vn.parent = &dev_dir;
}

static long mem_read(struct vnode *vn, uint64_t off, void *buf, size_t n)
{
    struct memfile *m = vn->priv;

    if (off >= vn->size)
        return 0;
    if (n > vn->size - off)
        n = (size_t)(vn->size - off);
    memcpy(buf, (const uint8_t *)m->data + off, n);
    return (long)n;
}

static const struct vnops mem_ops = {
    .read = mem_read,
};

void devfs_add_blob(const char *name, const void *data, size_t size)
{
    unsigned i;

    for (i = 0; i < 4; i++) {
        struct memfile *m = &memfiles[i];

        if (m->data)
            continue;
        memset(m, 0, sizeof(*m));
        m->data = data;
        m->vn.type = VREG;
        m->vn.ops = &mem_ops;
        m->vn.ino = vfs_next_ino();
        m->vn.size = size;
        m->vn.priv = m;
        m->vn.mode = 0100444;
        m->vn.nlink = 1;
        memcpy(m->vn.name, name, strlen(name) + 1);
        return;
    }
}

static bool ieq(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
        char y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;

        if (x != y)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

bool devfs_resolve_root(const char *spec, struct blkdrv **parent, uint64_t *start, uint64_t *nsect,
                        const char **name)
{
    unsigned i;

    if (!memcmp(spec, "/dev/", 5))
        spec += 5;
    for (i = 0; i < MAX_NODES; i++) {
        struct devnode *d = &nodes[i];
        bool match;

        if (!d->used)
            continue;
        if (!memcmp(spec, "PARTUUID=", 9))
            match = !d->whole && ieq(d->uuid, spec + 9);
        else if (!memcmp(spec, "PARTLABEL=", 10))
            match = !d->whole && !strcmp(d->label, spec + 10);
        else
            match = !strcmp(d->vn.name, spec);
        if (!match)
            continue;
        *parent = d->drv;
        *start = d->start;
        *nsect = d->nsect;
        *name = d->vn.name;
        return true;
    }
    return false;
}

static int part_read(void *priv, uint32_t blk, void *buf)
{
    struct partdrv *p = priv;

    return blk_rw_bytes(p->parent, p->start * 512 + (uint64_t)blk * LEMON_BSIZE, buf, LEMON_BSIZE, 0);
}

static int part_write(void *priv, uint32_t blk, const void *buf)
{
    struct partdrv *p = priv;

    return blk_rw_bytes(p->parent, p->start * 512 + (uint64_t)blk * LEMON_BSIZE, (void *)buf, LEMON_BSIZE, 1);
}

static int part_flush(void *priv)
{
    struct partdrv *p = priv;

    return blk_flush(p->parent);
}

struct blkdrv *devfs_part_drv(struct blkdrv *parent, uint64_t start, uint64_t nsect, const char *name)
{
    unsigned i;
    size_t n = strlen(name);

    for (i = 0; i < 2; i++) {
        struct partdrv *p = &partdrvs[i];

        if (p->used)
            continue;
        memset(p, 0, sizeof(*p));
        p->used = true;
        p->parent = parent;
        p->start = start;
        if (n >= sizeof(p->name))
            n = sizeof(p->name) - 1;
        memcpy(p->name, name, n);
        p->drv.name = p->name;
        p->drv.priv = p;
        p->drv.nsectors = nsect;
        p->drv.nblocks = (uint32_t)(nsect / (LEMON_BSIZE / 512));
        p->drv.read = part_read;
        p->drv.write = part_write;
        p->drv.flush = part_flush;
        memcpy(p->drv.model, parent->model, sizeof(p->drv.model));
        return &p->drv;
    }
    return NULL;
}
