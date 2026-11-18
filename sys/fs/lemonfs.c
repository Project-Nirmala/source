#include <vfs.h>
#include <lemonfs.h>
#include <kern.h>
#include <mm.h>
#include <cpu.h>
#include <wallclock.h>
#include <sys/errno.h>

static struct lemon  L;
static struct vnode **icache;
static const struct vnops lemon_vnops;
static const struct fsops lemon_fsops;

static uint32_t lemon_now(void)
{
    return (uint32_t)wall_seconds();
}

static uint32_t ino_of(const struct vnode *vn)
{
    return (uint32_t)(uintptr_t)vn->priv;
}

static void set_name(struct vnode *v, const char *name)
{
    size_t n = strlen(name);

    if (n > NAME_MAX)
        n = NAME_MAX;
    memcpy(v->name, name, n);
    v->name[n] = 0;
}

static struct vnode *vnode_for(uint32_t ino, struct vnode *parent, const char *name)
{
    struct lemon_inode ip;
    struct vnode *v = icache[ino];

    if (v)
        return v;
    if (lemon_iget(&L, ino, &ip) < 0)
        return NULL;
    v = kzalloc(sizeof(*v));
    if (!v)
        return NULL;
    v->type = (ip.mode & LEMON_IFMT) == LEMON_IFDIR ? VDIR : VREG;
    v->ops = &lemon_vnops;
    v->fs = &lemon_fsops;
    v->ino = ino;
    v->priv = (void *)(uintptr_t)ino;
    v->size = ip.size;
    v->mode = ip.mode;
    v->nlink = ip.nlink;
    v->mtime = ip.mtime;
    v->parent = parent;
    if (name)
        set_name(v, name);
    icache[ino] = v;
    return v;
}

static void inode_drop(uint32_t ino)
{
    struct vnode *v = icache[ino];

    lemon_ifree(&L, ino);
    icache[ino] = NULL;
    if (!v)
        return;
    if (v->type == VREG)
        kfree(v);
    else
        v->priv = NULL;
}

static long lemon_vread(struct vnode *vn, uint64_t off, void *buf, size_t n)
{
    if (!vn->priv)
        return -ENOENT;
    return lemon_read(&L, ino_of(vn), off, buf, n);
}

static long lemon_vwrite(struct vnode *vn, uint64_t off, const void *buf, size_t n)
{
    long r;

    if (!vn->priv)
        return -ENOENT;
    r = lemon_write(&L, ino_of(vn), off, buf, n);
    if (r > 0) {
        if (off + (uint64_t)r > vn->size)
            vn->size = off + (uint64_t)r;
        vn->mtime = lemon_now();
    }
    return r;
}

static void lemon_release(struct vnode *vn)
{
    if (vn->refs == 0 && vn->nlink == 0 && vn->priv)
        inode_drop(ino_of(vn));
}

static const struct vnops lemon_vnops = {
    .read = lemon_vread,
    .write = lemon_vwrite,
    .release = lemon_release,
};

static int lemon_lookup(struct vnode *dir, const char *name, struct vnode **out)
{
    uint32_t ino;
    struct vnode *v;
    int type, r;

    if (!dir->priv)
        return -ENOENT;
    r = lemon_dir_lookup(&L, ino_of(dir), name, &ino, &type);
    if (r < 0)
        return r;
    v = vnode_for(ino, dir, name);
    if (!v)
        return -ENOMEM;
    *out = v;
    return 0;
}

static int lemon_create(struct vnode *dir, const char *name, uint32_t mode, int isdir, struct vnode **out)
{
    uint32_t ino;
    struct vnode *v;
    int type, r;

    if (!dir->priv)
        return -ENOENT;
    r = lemon_dir_lookup(&L, ino_of(dir), name, &ino, &type);
    if (r == 0)
        return -EEXIST;
    if (r != -ENOENT)
        return r;
    r = lemon_ialloc(&L, (uint16_t)((isdir ? LEMON_IFDIR : LEMON_IFREG) | (mode & 07777)), &ino);
    if (r < 0)
        return r;
    r = lemon_dir_add(&L, ino_of(dir), name, ino, isdir ? LEMON_DT_DIR : LEMON_DT_REG);
    if (r < 0) {
        lemon_ifree(&L, ino);
        return r;
    }
    v = vnode_for(ino, dir, name);
    if (!v)
        return -ENOMEM;
    dir->mtime = lemon_now();
    *out = v;
    return 0;
}

static void release_entry(uint32_t ino)
{
    struct vnode *v = icache[ino];

    if (v && v->refs > 0) {
        v->nlink = 0;
        return;
    }
    inode_drop(ino);
}

static int lemon_remove(struct vnode *dir, const char *name, int want_dir)
{
    uint32_t ino;
    int type, r;

    if (!dir->priv)
        return -ENOENT;
    r = lemon_dir_lookup(&L, ino_of(dir), name, &ino, &type);
    if (r < 0)
        return r;
    if (want_dir && type != LEMON_DT_DIR)
        return -ENOTDIR;
    if (!want_dir && type == LEMON_DT_DIR)
        return -EISDIR;
    if (want_dir && lemon_dir_empty(&L, ino) != 1)
        return -ENOTEMPTY;
    r = lemon_dir_remove(&L, ino_of(dir), name);
    if (r < 0)
        return r;
    release_entry(ino);
    return 0;
}

static int lemon_rename(struct vnode *od, const char *on, struct vnode *nd, const char *nn)
{
    uint32_t sino, dino;
    int stype, dtype, r;
    struct vnode *v;

    if (!od->priv || !nd->priv)
        return -ENOENT;
    r = lemon_dir_lookup(&L, ino_of(od), on, &sino, &stype);
    if (r < 0)
        return r;
    r = lemon_dir_lookup(&L, ino_of(nd), nn, &dino, &dtype);
    if (r == 0) {
        if (dino == sino)
            return 0;
        if (stype == LEMON_DT_DIR && dtype != LEMON_DT_DIR)
            return -ENOTDIR;
        if (stype != LEMON_DT_DIR && dtype == LEMON_DT_DIR)
            return -EISDIR;
        if (dtype == LEMON_DT_DIR && lemon_dir_empty(&L, dino) != 1)
            return -ENOTEMPTY;
        r = lemon_dir_remove(&L, ino_of(nd), nn);
        if (r < 0)
            return r;
        release_entry(dino);
    } else if (r != -ENOENT) {
        return r;
    }
    r = lemon_dir_add(&L, ino_of(nd), nn, sino, stype);
    if (r < 0)
        return r;
    lemon_dir_remove(&L, ino_of(od), on);
    v = icache[sino];
    if (v) {
        v->parent = nd;
        set_name(v, nn);
    }
    return 0;
}

static int lemon_readdir(struct vnode *dir, uint64_t idx, char *name, struct vnode **ent)
{
    uint32_t ino;
    int type, r;
    struct vnode *v;

    if (!dir->priv)
        return 0;
    r = lemon_dir_read(&L, ino_of(dir), idx, name, &ino, &type);
    if (r <= 0)
        return r;
    v = vnode_for(ino, dir, name);
    if (!v)
        return -ENOMEM;
    *ent = v;
    return 1;
}

static int lemon_truncate_vn(struct vnode *vn, uint64_t size)
{
    int r;

    if (!vn->priv)
        return -ENOENT;
    r = lemon_truncate(&L, ino_of(vn), size);
    if (r == 0) {
        vn->size = size;
        vn->mtime = lemon_now();
    }
    return r;
}

static int lemon_statfs(struct vnode *vn, struct vstatfs *out)
{
    (void)vn;
    out->bsize = LEMON_BSIZE;
    out->blocks = L.sb.nblocks - L.sb.data_blk;
    out->bfree = L.sb.free_blocks;
    out->files = L.sb.ninodes - 1;
    out->ffree = L.sb.free_inodes;
    out->namelen = LEMON_NAME_MAX;
    return 0;
}

static int lemon_settime(struct vnode *vn, uint32_t mtime)
{
    int r;

    if (!vn->priv)
        return -ENOENT;
    r = lemon_set_mtime(&L, ino_of(vn), mtime);
    if (r == 0)
        vn->mtime = mtime;
    return r;
}

static const struct fsops lemon_fsops = {
    .lookup = lemon_lookup,
    .create = lemon_create,
    .remove = lemon_remove,
    .rename = lemon_rename,
    .readdir = lemon_readdir,
    .truncate = lemon_truncate_vn,
    .statfs = lemon_statfs,
    .settime = lemon_settime,
};

int lemonfs_probe(struct blkdev *dev)
{
    struct lemon tmp;

    return lemon_mount(&tmp, dev);
}

int lemonfs_check(void)
{
    uint8_t *scratch = kmalloc(L.sb.nblocks);
    int bad;

    if (!scratch)
        return 0;
    L.log = kprintf;
    bad = lemon_check(&L, scratch);
    kfree(scratch);
    return bad;
}

struct vnode *lemonfs_mount(struct blkdev *dev)
{
    struct vnode *root;

    if (lemon_mount(&L, dev) < 0)
        panic("lemonfs: bad superblock");
    L.now = lemon_now;
    icache = kzalloc(sizeof(*icache) * L.sb.ninodes);
    if (!icache)
        panic("lemonfs: out of memory");
    root = vnode_for(LEMON_ROOT_INO, NULL, "");
    if (!root)
        panic("lemonfs: no root inode");
    return root;
}
