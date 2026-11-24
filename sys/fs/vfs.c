#include <vfs.h>
#include <proc.h>
#include <blk.h>
#include <cmdline.h>
#include <kern.h>
#include <sys/errno.h>

#define RAMDISK_BLOCKS  4096
#define ROOT_INODES     1024
#define DISK_CACHE_BUFS 128

static struct vnode *root_vnode;
static uint64_t ino_counter = 100000;

uint64_t vfs_next_ino(void)
{
    return ino_counter++;
}

static bool disk_is_blank(struct blkdev *dev)
{
    uint8_t *p = dev->get(dev, 0);
    size_t i;
    bool blank = true;

    for (i = 0; i < LEMON_BSIZE; i++)
        if (p[i]) {
            blank = false;
            break;
        }
    dev->put(dev, 0, 0);
    return blank;
}

static struct blkdev *ramdisk_with_fs(void)
{
    struct blkdev *dev = ramdisk_create(RAMDISK_BLOCKS);

    if (!dev)
        panic("vfs: cannot allocate ramdisk");
    if (lemon_mkfs(dev, ROOT_INODES, "nirmala") < 0)
        panic("vfs: mkfs failed");
    return dev;
}

void vfs_init(const void *image, size_t size)
{
    struct blkdrv *vdrv = virtio_blk_probe();
    struct blkdrv *sdrv = ahci_probe();
    struct blkdrv *ndrv = nvme_probe();
    struct blkdrv *drv = NULL, *rparent = NULL;
    struct blkdev *dev = NULL;
    uint64_t rstart = 0, rnsect = 0;
    const char *rname = NULL;
    char spec[96];
    bool persistent = false, fresh = false;

    if (!sdrv)
        sdrv = ndrv;
    if (!sdrv)
        sdrv = ide_probe();
    devfs_add_blob("initrd", image, size);
    devfs_init();
    if (!cmdline_value("root", spec, sizeof(spec)))
        spec[0] = 0;
    if (!strcmp(spec, "auto")) {
        rparent = vdrv ? vdrv : sdrv;
        if (rparent) {
            rstart = 0;
            rnsect = rparent->nsectors;
            drv = rparent;
        }
    } else if (spec[0] && strcmp(spec, "ram") && strcmp(spec, "live")) {
        if (devfs_resolve_root(spec, &rparent, &rstart, &rnsect, &rname))
            drv = devfs_part_drv(rparent, rstart, rnsect, rname);
        else
            kprintf("root=%s not found, using a RAM disk\n", spec);
    }

    if (drv) {
        dev = blkcache_create(drv, DISK_CACHE_BUFS);
        if (!dev)
            panic("vfs: out of memory for the block cache");
        if (lemonfs_probe(dev) == 0) {
            persistent = true;
        } else if (disk_is_blank(dev)) {
            uint32_t ninodes = drv->nblocks / 4;

            if (ninodes < ROOT_INODES)
                ninodes = ROOT_INODES;
            if (ninodes > 65536)
                ninodes = 65536;
            if (lemon_mkfs(dev, ninodes, "nirmala") < 0)
                panic("vfs: mkfs failed on %s", drv->name);
            persistent = fresh = true;
        } else {
            kprintf("%s: not a lemonfs disk, leaving it untouched; using a RAM disk\n", drv->name);
            dev = NULL;
        }
    }
    if (!dev)
        dev = ramdisk_with_fs();
    root_vnode = lemonfs_mount(dev);
    if (persistent) {
        int bad = fresh ? 0 : lemonfs_check();

        blk_set_root(rparent, rstart, rnsect);
        kprintf("[ok] root: %s %lu MiB, lemonfs %s\n", drv->name,
                (uint64_t)drv->nblocks * (LEMON_BSIZE / 1024) / 1024, fresh ? "created" : "mounted");
        if (bad)
            kprintf("lemonfs: %d consistency problems found (not repaired)\n", bad);
    } else {
        kprintf("[ok] root: volatile RAM disk\n");
    }
    devfs_attach(root_vnode);
    tarfs_extract(image, size);
    vfs_mkdir("/dev", root_vnode, 0755);
}

struct vnode *vfs_root(void)
{
    return root_vnode;
}

static uint32_t cred_uid(void)
{
    return curproc ? curproc->uid : 0;
}

static uint32_t cred_gid(void)
{
    return curproc ? curproc->gid : 0;
}

/*
 * Classic owner/group/other check. Nodes that carry no mode (pipes, the
 * console) get the same defaults stat() reports, except that character
 * devices stay open to everyone. uid 0 passes everything, including files
 * that lack an execute bit (older nirld output was created without one).
 */
int vfs_perm(const struct vnode *vn, int want)
{
    uint32_t mode = vn->mode & 0777, uid = cred_uid(), gid = cred_gid();
    int have;

    if (!vn->mode) {
        if (vn->type == VCHR || vn->type == VPIPE)
            return 0;
        mode = vn->type == VDIR ? 0755 : vn->type == VBLK ? 0660 : 0644;
    }
    if (uid == 0)
        return 0;
    if (uid == vn->uid)
        have = (int)((mode >> 6) & 7);
    else if (gid == vn->gid)
        have = (int)((mode >> 3) & 7);
    else
        have = (int)(mode & 7);
    return (have & want) == want ? 0 : -EACCES;
}

/* May the caller remove or rename 'victim' out of 'dir'? (sticky directories) */
static int sticky_ok(const struct vnode *dir, const struct vnode *victim)
{
    uint32_t uid = cred_uid();

    if (!(dir->mode & M_SVTX) || uid == 0 || uid == dir->uid || (victim && uid == victim->uid))
        return 0;
    return -EPERM;
}

int vfs_chattr(struct vnode *vn, int32_t mode, int64_t uid, int64_t gid)
{
    uint32_t cu = cred_uid();

    if (!vn->fs || !vn->fs->setattr)
        return -EPERM;
    if (cu != 0) {
        if (uid != -1 && (uint32_t)uid != vn->uid)
            return -EPERM;
        if (cu != vn->uid)
            return -EPERM;
        if (gid != -1 && (uint32_t)gid != cred_gid())
            return -EPERM;
    }
    return vn->fs->setattr(vn, (uint32_t)mode, (uint32_t)uid, (uint32_t)gid);
}

static int step(struct vnode *dir, const char *name, struct vnode **out)
{
    if (dir->type != VDIR)
        return -ENOTDIR;
    if (vfs_perm(dir, VP_X) < 0)
        return -EACCES;
    if (!strcmp(name, ".")) {
        *out = dir;
        return 0;
    }
    if (!strcmp(name, "..")) {
        *out = dir->parent ? dir->parent : dir;
        return 0;
    }
    if (dir == root_vnode && !strcmp(name, "dev")) {
        *out = devfs_root();
        return 0;
    }
    if (!dir->fs || !dir->fs->lookup)
        return -ENOENT;
    return dir->fs->lookup(dir, name, out);
}

int vfs_lookup(const char *path, struct vnode *cwd, struct vnode **out)
{
    struct vnode *cur = path[0] == '/' ? root_vnode : cwd;
    char comp[NAME_MAX + 1];

    if (!cur)
        return -ENOENT;
    if (!path[0])
        return -ENOENT;
    while (*path) {
        size_t n = 0;
        int r;

        while (*path == '/')
            path++;
        if (!*path)
            break;
        while (path[n] && path[n] != '/')
            n++;
        if (n > NAME_MAX)
            return -ENAMETOOLONG;
        memcpy(comp, path, n);
        comp[n] = 0;
        path += n;
        r = step(cur, comp, &cur);
        if (r < 0)
            return r;
    }
    *out = cur;
    return 0;
}

static int lookup_parent(const char *path, struct vnode *cwd, struct vnode **dir, char *name)
{
    char buf[PATH_MAX];
    size_t len = strlen(path), i, last;
    int r;

    if (len == 0)
        return -ENOENT;
    if (len >= sizeof(buf))
        return -ENAMETOOLONG;
    memcpy(buf, path, len + 1);
    while (len > 1 && buf[len - 1] == '/')
        buf[--len] = 0;
    for (i = 0, last = 0; i < len; i++)
        if (buf[i] == '/')
            last = i + 1;
    if (last == len)
        return -EEXIST;
    if (len - last > NAME_MAX)
        return -ENAMETOOLONG;
    memcpy(name, buf + last, len - last + 1);
    if (!strcmp(name, ".") || !strcmp(name, ".."))
        return -EINVAL;
    if (last == 0) {
        *dir = cwd;
    } else {
        if (last > 1)
            buf[last - 1] = 0;
        else
            buf[1] = 0;
        r = vfs_lookup(buf, cwd, dir);
        if (r < 0)
            return r;
    }
    if (!*dir || (*dir)->type != VDIR)
        return -ENOTDIR;
    if (!(*dir)->fs)
        return -EROFS;
    return 0;
}

int vfs_create(const char *path, struct vnode *cwd, uint32_t mode, int excl, struct vnode **out)
{
    struct vnode *dir, *v;
    char name[NAME_MAX + 1];
    int r = lookup_parent(path, cwd, &dir, name);

    if (r < 0)
        return r == -EEXIST ? -EISDIR : r;
    r = dir->fs->lookup(dir, name, &v);
    if (r == 0) {
        if (excl)
            return -EEXIST;
        if (v->type == VDIR)
            return -EISDIR;
        *out = v;
        return 0;
    }
    if (r != -ENOENT)
        return r;
    if (vfs_perm(dir, VP_W | VP_X) < 0)
        return -EACCES;
    return dir->fs->create(dir, name, mode, 0, out);
}

int vfs_mkdir(const char *path, struct vnode *cwd, uint32_t mode)
{
    struct vnode *dir, *v;
    char name[NAME_MAX + 1];
    int r = lookup_parent(path, cwd, &dir, name);

    if (r == -EINVAL)
        return -EEXIST;
    if (r < 0)
        return r;
    if (vfs_perm(dir, VP_W | VP_X) < 0)
        return -EACCES;
    return dir->fs->create(dir, name, mode, 1, &v);
}

static int remove_path(const char *path, struct vnode *cwd, int want_dir)
{
    struct vnode *dir, *victim;
    char name[NAME_MAX + 1];
    int r = lookup_parent(path, cwd, &dir, name);

    if (r == -EEXIST)
        return -EBUSY;
    if (r < 0)
        return r;
    if (vfs_perm(dir, VP_W | VP_X) < 0)
        return -EACCES;
    if (dir->fs->lookup(dir, name, &victim) == 0 && sticky_ok(dir, victim) < 0)
        return -EPERM;
    return dir->fs->remove(dir, name, want_dir);
}

int vfs_unlink(const char *path, struct vnode *cwd)
{
    return remove_path(path, cwd, 0);
}

int vfs_rmdir(const char *path, struct vnode *cwd)
{
    return remove_path(path, cwd, 1);
}

int vfs_rename(const char *from, const char *to, struct vnode *cwd)
{
    struct vnode *od, *nd, *src, *t;
    char on[NAME_MAX + 1], nn[NAME_MAX + 1];
    int r = lookup_parent(from, cwd, &od, on);

    if (r == -EEXIST)
        return -EBUSY;
    if (r < 0)
        return r;
    r = lookup_parent(to, cwd, &nd, nn);
    if (r == -EEXIST)
        return -EBUSY;
    if (r < 0)
        return r;
    if (od->fs != nd->fs || !od->fs->rename)
        return -EXDEV;
    r = od->fs->lookup(od, on, &src);
    if (r < 0)
        return r;
    if (vfs_perm(od, VP_W | VP_X) < 0 || vfs_perm(nd, VP_W | VP_X) < 0)
        return -EACCES;
    if (sticky_ok(od, src) < 0)
        return -EPERM;
    if (src->type == VDIR)
        for (t = nd; t; t = t->parent)
            if (t == src)
                return -EINVAL;
    return od->fs->rename(od, on, nd, nn);
}

int vfs_truncate(struct vnode *vn, uint64_t size)
{
    if (vn->type == VDIR)
        return -EISDIR;
    if (!vn->fs || !vn->fs->truncate)
        return -EINVAL;
    return vn->fs->truncate(vn, size);
}

int vfs_utime(struct vnode *vn, uint32_t mtime)
{
    if (!vn->fs || !vn->fs->settime)
        return -ENOSYS;
    return vn->fs->settime(vn, mtime);
}

int vfs_statfs(struct vnode *vn, struct vstatfs *out)
{
    if (!vn->fs || !vn->fs->statfs)
        return -ENOSYS;
    return vn->fs->statfs(vn, out);
}

long vnode_read(struct vnode *vn, uint64_t off, void *buf, size_t n)
{
    if (vn->type == VDIR)
        return -EISDIR;
    if (!vn->ops || !vn->ops->read)
        return -EINVAL;
    return vn->ops->read(vn, off, buf, n);
}

long vnode_write(struct vnode *vn, uint64_t off, const void *buf, size_t n)
{
    if (vn->type == VDIR)
        return -EISDIR;
    if (!vn->ops || !vn->ops->write)
        return -EROFS;
    return vn->ops->write(vn, off, buf, n);
}

long vnode_ioctl(struct vnode *vn, uint64_t req, uint64_t arg)
{
    if (!vn->ops || !vn->ops->ioctl)
        return -ENOTTY;
    return vn->ops->ioctl(vn, req, arg);
}

int vfs_readdir(struct vnode *dir, uint64_t idx, struct vnode **ent, char *name)
{
    if (idx == 0) {
        *ent = dir;
        memcpy(name, ".", 2);
        return 1;
    }
    if (idx == 1) {
        *ent = dir->parent ? dir->parent : dir;
        memcpy(name, "..", 3);
        return 1;
    }
    if (!dir->fs || !dir->fs->readdir)
        return 0;
    return dir->fs->readdir(dir, idx - 2, name, ent) > 0;
}

long vfs_getpath(struct vnode *vn, char *buf, size_t size)
{
    struct vnode *chain[32];
    size_t depth = 0, len = 0, i;

    for (; vn && vn != root_vnode; vn = vn->parent) {
        if (depth == 32)
            return -ENAMETOOLONG;
        chain[depth++] = vn;
    }
    if (depth == 0) {
        if (size < 2)
            return -ERANGE;
        buf[0] = '/';
        buf[1] = 0;
        return 1;
    }
    for (i = depth; i > 0; i--) {
        size_t n = strlen(chain[i - 1]->name);

        if (len + 1 + n + 1 > size)
            return -ERANGE;
        buf[len++] = '/';
        memcpy(buf + len, chain[i - 1]->name, n);
        len += n;
    }
    buf[len] = 0;
    return (long)len;
}
