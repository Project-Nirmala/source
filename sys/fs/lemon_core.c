#include <lemonfs.h>
#include <sys/errno.h>

#ifdef LEMON_HOST_TEST
#include <string.h>
#else
#include <kern.h>
#endif

typedef char lemon_inode_size_check[sizeof(struct lemon_inode) == LEMON_ISIZE ? 1 : -1];
typedef char lemon_dirent_size_check[sizeof(struct lemon_dirent) == LEMON_DSIZE ? 1 : -1];

#define BITS_PER_BLK (LEMON_BSIZE * 8u)

static uint8_t *bget(struct lemon *l, uint32_t blk)
{
    return l->dev->get(l->dev, blk);
}

static void bput(struct lemon *l, uint32_t blk, int dirty)
{
    l->dev->put(l->dev, blk, dirty);
}

static void sb_sync(struct lemon *l)
{
    uint8_t *p = bget(l, 0);

    memcpy(p, &l->sb, sizeof(l->sb));
    bput(l, 0, 1);
}

static uint32_t now(struct lemon *l)
{
    return l->now ? l->now() : 0;
}

static int bm_test(struct lemon *l, uint32_t first_blk, uint32_t i)
{
    uint32_t blk = first_blk + i / BITS_PER_BLK, bit = i % BITS_PER_BLK;
    uint8_t *p = bget(l, blk);
    int v = (p[bit >> 3] >> (bit & 7)) & 1;

    bput(l, blk, 0);
    return v;
}

static void bm_set(struct lemon *l, uint32_t first_blk, uint32_t i, int val)
{
    uint32_t blk = first_blk + i / BITS_PER_BLK, bit = i % BITS_PER_BLK;
    uint8_t *p = bget(l, blk);

    if (val)
        p[bit >> 3] |= (uint8_t)(1u << (bit & 7));
    else
        p[bit >> 3] &= (uint8_t)~(1u << (bit & 7));
    bput(l, blk, 1);
}

static int bm_alloc(struct lemon *l, uint32_t first_blk, uint32_t nbits, uint32_t lo,
                    uint32_t start, uint32_t *out)
{
    uint32_t n, span = nbits - lo, cur_blk = 0;
    uint8_t *p = 0;

    if (start < lo || start >= nbits)
        start = lo;
    for (n = 0; n < span; n++) {
        uint32_t i = lo + (start - lo + n) % span;
        uint32_t blk = first_blk + i / BITS_PER_BLK, bit = i % BITS_PER_BLK;

        if (!p || blk != cur_blk) {
            if (p)
                bput(l, cur_blk, 0);
            p = bget(l, blk);
            cur_blk = blk;
        }
        if (!(p[bit >> 3] & (1u << (bit & 7)))) {
            p[bit >> 3] |= (uint8_t)(1u << (bit & 7));
            bput(l, cur_blk, 1);
            *out = i;
            return 0;
        }
    }
    if (p)
        bput(l, cur_blk, 0);
    return -1;
}

static int balloc(struct lemon *l, uint32_t *out)
{
    uint32_t b;
    uint8_t *p;

    if (bm_alloc(l, l->sb.bbmap_blk, l->sb.nblocks, l->sb.data_blk, l->bhint, &b) < 0)
        return -ENOSPC;
    l->bhint = b + 1;
    p = bget(l, b);
    memset(p, 0, LEMON_BSIZE);
    bput(l, b, 1);
    l->sb.free_blocks--;
    sb_sync(l);
    *out = b;
    return 0;
}

static void bfree(struct lemon *l, uint32_t b)
{
    if (b < l->sb.data_blk || b >= l->sb.nblocks)
        return;
    bm_set(l, l->sb.bbmap_blk, b, 0);
    l->sb.free_blocks++;
    if (b < l->bhint)
        l->bhint = b;
    sb_sync(l);
}

int lemon_iget(struct lemon *l, uint32_t ino, struct lemon_inode *ip)
{
    uint32_t blk;
    uint8_t *p;

    if (ino == 0 || ino >= l->sb.ninodes)
        return -EINVAL;
    blk = l->sb.itab_blk + ino / LEMON_IPB;
    p = bget(l, blk);
    memcpy(ip, p + (ino % LEMON_IPB) * LEMON_ISIZE, sizeof(*ip));
    bput(l, blk, 0);
    return 0;
}

int lemon_iput(struct lemon *l, uint32_t ino, const struct lemon_inode *ip)
{
    uint32_t blk;
    uint8_t *p;

    if (ino == 0 || ino >= l->sb.ninodes)
        return -EINVAL;
    blk = l->sb.itab_blk + ino / LEMON_IPB;
    p = bget(l, blk);
    memcpy(p + (ino % LEMON_IPB) * LEMON_ISIZE, ip, sizeof(*ip));
    bput(l, blk, 1);
    return 0;
}

int lemon_ialloc(struct lemon *l, uint16_t mode, uint32_t *ino)
{
    struct lemon_inode ip;
    uint32_t i;

    if (bm_alloc(l, l->sb.ibmap_blk, l->sb.ninodes, 1, l->ihint, &i) < 0)
        return -ENOSPC;
    l->ihint = i + 1;
    memset(&ip, 0, sizeof(ip));
    ip.mode = mode;
    ip.nlink = 1;
    ip.mtime = ip.ctime = now(l);
    lemon_iput(l, i, &ip);
    l->sb.free_inodes--;
    sb_sync(l);
    *ino = i;
    return 0;
}

static int bmap(struct lemon *l, struct lemon_inode *ip, uint64_t fbn, int alloc, uint32_t *out)
{
    uint32_t b, *slot, *p;
    int r;

    *out = 0;
    if (fbn < LEMON_NDIRECT) {
        slot = &ip->direct[fbn];
        if (!*slot) {
            if (!alloc)
                return 0;
            if ((r = balloc(l, &b)) < 0)
                return r;
            *slot = b;
        }
        *out = *slot;
        return 0;
    }
    fbn -= LEMON_NDIRECT;
    if (fbn < LEMON_PPB) {
        if (!ip->indirect) {
            if (!alloc)
                return 0;
            if ((r = balloc(l, &b)) < 0)
                return r;
            ip->indirect = b;
        }
        p = (uint32_t *)bget(l, ip->indirect);
        if (!p[fbn]) {
            if (!alloc) {
                bput(l, ip->indirect, 0);
                return 0;
            }
            if ((r = balloc(l, &b)) < 0) {
                bput(l, ip->indirect, 0);
                return r;
            }
            p[fbn] = b;
            bput(l, ip->indirect, 1);
        } else {
            bput(l, ip->indirect, 0);
        }
        p = (uint32_t *)bget(l, ip->indirect);
        *out = p[fbn];
        bput(l, ip->indirect, 0);
        return 0;
    }
    fbn -= LEMON_PPB;
    if (fbn < (uint64_t)LEMON_PPB * LEMON_PPB) {
        uint32_t i1 = (uint32_t)(fbn / LEMON_PPB), i2 = (uint32_t)(fbn % LEMON_PPB), mid;

        if (!ip->dindirect) {
            if (!alloc)
                return 0;
            if ((r = balloc(l, &b)) < 0)
                return r;
            ip->dindirect = b;
        }
        p = (uint32_t *)bget(l, ip->dindirect);
        mid = p[i1];
        if (!mid) {
            if (!alloc) {
                bput(l, ip->dindirect, 0);
                return 0;
            }
            if ((r = balloc(l, &b)) < 0) {
                bput(l, ip->dindirect, 0);
                return r;
            }
            p[i1] = mid = b;
            bput(l, ip->dindirect, 1);
        } else {
            bput(l, ip->dindirect, 0);
        }
        p = (uint32_t *)bget(l, mid);
        if (!p[i2]) {
            if (!alloc) {
                bput(l, mid, 0);
                return 0;
            }
            if ((r = balloc(l, &b)) < 0) {
                bput(l, mid, 0);
                return r;
            }
            p[i2] = b;
            bput(l, mid, 1);
            *out = b;
            return 0;
        }
        *out = p[i2];
        bput(l, mid, 0);
        return 0;
    }
    return -EFBIG;
}

static void unmap_block(struct lemon *l, struct lemon_inode *ip, uint64_t fbn)
{
    uint32_t *p, mid, b;

    if (fbn < LEMON_NDIRECT) {
        if (ip->direct[fbn]) {
            bfree(l, ip->direct[fbn]);
            ip->direct[fbn] = 0;
        }
        return;
    }
    fbn -= LEMON_NDIRECT;
    if (fbn < LEMON_PPB) {
        if (!ip->indirect)
            return;
        p = (uint32_t *)bget(l, ip->indirect);
        b = p[fbn];
        p[fbn] = 0;
        bput(l, ip->indirect, 1);
        if (b)
            bfree(l, b);
        return;
    }
    fbn -= LEMON_PPB;
    if (!ip->dindirect)
        return;
    p = (uint32_t *)bget(l, ip->dindirect);
    mid = p[fbn / LEMON_PPB];
    bput(l, ip->dindirect, 0);
    if (!mid)
        return;
    p = (uint32_t *)bget(l, mid);
    b = p[fbn % LEMON_PPB];
    p[fbn % LEMON_PPB] = 0;
    bput(l, mid, 1);
    if (b)
        bfree(l, b);
}

static int block_empty(struct lemon *l, uint32_t blk)
{
    uint32_t *p = (uint32_t *)bget(l, blk);
    uint32_t i;
    int empty = 1;

    for (i = 0; i < LEMON_PPB; i++)
        if (p[i]) {
            empty = 0;
            break;
        }
    bput(l, blk, 0);
    return empty;
}

static void reap_indirect(struct lemon *l, struct lemon_inode *ip)
{
    uint32_t i;

    if (ip->indirect && block_empty(l, ip->indirect)) {
        bfree(l, ip->indirect);
        ip->indirect = 0;
    }
    if (ip->dindirect) {
        for (i = 0; i < LEMON_PPB; i++) {
            uint32_t *p = (uint32_t *)bget(l, ip->dindirect), mid = p[i];

            bput(l, ip->dindirect, 0);
            if (mid && block_empty(l, mid)) {
                bfree(l, mid);
                p = (uint32_t *)bget(l, ip->dindirect);
                p[i] = 0;
                bput(l, ip->dindirect, 1);
            }
        }
        if (block_empty(l, ip->dindirect)) {
            bfree(l, ip->dindirect);
            ip->dindirect = 0;
        }
    }
}

static int truncate_inode(struct lemon *l, struct lemon_inode *ip, uint64_t size)
{
    uint64_t old = ip->size;

    if (size < old) {
        uint64_t first = (size + LEMON_BSIZE - 1) / LEMON_BSIZE;
        uint64_t last = (old + LEMON_BSIZE - 1) / LEMON_BSIZE, f;

        for (f = first; f < last; f++)
            unmap_block(l, ip, f);
        reap_indirect(l, ip);
        if (size % LEMON_BSIZE) {
            uint32_t b;

            if (bmap(l, ip, size / LEMON_BSIZE, 0, &b) == 0 && b) {
                uint8_t *p = bget(l, b);

                memset(p + size % LEMON_BSIZE, 0, LEMON_BSIZE - size % LEMON_BSIZE);
                bput(l, b, 1);
            }
        }
    }
    ip->size = size;
    ip->mtime = now(l);
    return 0;
}

int lemon_truncate(struct lemon *l, uint32_t ino, uint64_t size)
{
    struct lemon_inode ip;
    int r = lemon_iget(l, ino, &ip);

    if (r < 0)
        return r;
    if ((ip.mode & LEMON_IFMT) != LEMON_IFREG)
        return -EINVAL;
    if (size > (uint64_t)(LEMON_NDIRECT + LEMON_PPB + (uint64_t)LEMON_PPB * LEMON_PPB) * LEMON_BSIZE)
        return -EFBIG;
    truncate_inode(l, &ip, size);
    return lemon_iput(l, ino, &ip);
}

int lemon_set_mtime(struct lemon *l, uint32_t ino, uint32_t mtime)
{
    struct lemon_inode ip;
    int r = lemon_iget(l, ino, &ip);

    if (r < 0)
        return r;
    ip.mtime = mtime;
    return lemon_iput(l, ino, &ip);
}

int lemon_ifree(struct lemon *l, uint32_t ino)
{
    struct lemon_inode ip;
    int r = lemon_iget(l, ino, &ip);

    if (r < 0)
        return r;
    truncate_inode(l, &ip, 0);
    memset(&ip, 0, sizeof(ip));
    lemon_iput(l, ino, &ip);
    bm_set(l, l->sb.ibmap_blk, ino, 0);
    l->sb.free_inodes++;
    if (ino < l->ihint)
        l->ihint = ino;
    sb_sync(l);
    return 0;
}

long lemon_read(struct lemon *l, uint32_t ino, uint64_t off, void *buf, size_t n)
{
    struct lemon_inode ip;
    uint8_t *out = buf;
    size_t done = 0;
    int r = lemon_iget(l, ino, &ip);

    if (r < 0)
        return r;
    if (off >= ip.size)
        return 0;
    if (n > ip.size - off)
        n = (size_t)(ip.size - off);
    while (done < n) {
        uint64_t pos = off + done;
        size_t boff = (size_t)(pos % LEMON_BSIZE), chunk = LEMON_BSIZE - boff;
        uint32_t b;

        if (chunk > n - done)
            chunk = n - done;
        r = bmap(l, &ip, pos / LEMON_BSIZE, 0, &b);
        if (r < 0)
            return done ? (long)done : r;
        if (!b) {
            memset(out + done, 0, chunk);
        } else {
            uint8_t *p = bget(l, b);

            memcpy(out + done, p + boff, chunk);
            bput(l, b, 0);
        }
        done += chunk;
    }
    return (long)done;
}

long lemon_write(struct lemon *l, uint32_t ino, uint64_t off, const void *buf, size_t n)
{
    struct lemon_inode ip;
    const uint8_t *in = buf;
    size_t done = 0;
    int r = lemon_iget(l, ino, &ip), err = 0;

    if (r < 0)
        return r;
    if (off + n < off || off + n > (uint64_t)(LEMON_NDIRECT + LEMON_PPB +
                                              (uint64_t)LEMON_PPB * LEMON_PPB) * LEMON_BSIZE)
        return -EFBIG;
    while (done < n) {
        uint64_t pos = off + done;
        size_t boff = (size_t)(pos % LEMON_BSIZE), chunk = LEMON_BSIZE - boff;
        uint32_t b;
        uint8_t *p;

        if (chunk > n - done)
            chunk = n - done;
        err = bmap(l, &ip, pos / LEMON_BSIZE, 1, &b);
        if (err < 0)
            break;
        p = bget(l, b);
        memcpy(p + boff, in + done, chunk);
        bput(l, b, 1);
        done += chunk;
    }
    if (done) {
        if (off + done > ip.size)
            ip.size = off + done;
        ip.mtime = now(l);
    }
    lemon_iput(l, ino, &ip);
    return done ? (long)done : err;
}

static int dir_inode(struct lemon *l, uint32_t dino, struct lemon_inode *ip)
{
    int r = lemon_iget(l, dino, ip);

    if (r < 0)
        return r;
    if ((ip->mode & LEMON_IFMT) != LEMON_IFDIR)
        return -ENOTDIR;
    return 0;
}

static int name_ok(const char *name, size_t *len)
{
    size_t n = strlen(name);

    *len = n;
    if (n == 0)
        return -ENOENT;
    if (n > LEMON_NAME_MAX)
        return -ENAMETOOLONG;
    return 0;
}

int lemon_dir_lookup(struct lemon *l, uint32_t dino, const char *name, uint32_t *ino, int *type)
{
    struct lemon_inode ip;
    size_t nlen;
    uint64_t k, nblk;
    int r = dir_inode(l, dino, &ip);

    if (r < 0)
        return r;
    if ((r = name_ok(name, &nlen)) < 0)
        return r;
    nblk = ip.size / LEMON_BSIZE;
    for (k = 0; k < nblk; k++) {
        uint32_t b, j;
        uint8_t *p;

        if (bmap(l, &ip, k, 0, &b) < 0 || !b)
            continue;
        p = bget(l, b);
        for (j = 0; j < LEMON_DPB; j++) {
            struct lemon_dirent *de = (struct lemon_dirent *)(p + j * LEMON_DSIZE);

            if (de->ino && de->namelen == nlen && !memcmp(de->name, name, nlen)) {
                *ino = de->ino;
                if (type)
                    *type = de->type;
                bput(l, b, 0);
                return 0;
            }
        }
        bput(l, b, 0);
    }
    return -ENOENT;
}

int lemon_dir_add(struct lemon *l, uint32_t dino, const char *name, uint32_t ino, int type)
{
    struct lemon_inode ip;
    size_t nlen;
    uint64_t k, nblk;
    uint32_t b;
    int r = dir_inode(l, dino, &ip);

    if (r < 0)
        return r;
    if ((r = name_ok(name, &nlen)) < 0)
        return r;
    nblk = ip.size / LEMON_BSIZE;
    for (k = 0; k <= nblk; k++) {
        uint32_t j;
        uint8_t *p;

        if (k == nblk) {
            r = bmap(l, &ip, k, 1, &b);
            if (r < 0)
                return r;
            ip.size += LEMON_BSIZE;
        } else if (bmap(l, &ip, k, 0, &b) < 0 || !b) {
            continue;
        }
        p = bget(l, b);
        for (j = 0; j < LEMON_DPB; j++) {
            struct lemon_dirent *de = (struct lemon_dirent *)(p + j * LEMON_DSIZE);

            if (de->ino)
                continue;
            memset(de, 0, sizeof(*de));
            de->ino = ino;
            de->type = (uint8_t)type;
            de->namelen = (uint8_t)nlen;
            memcpy(de->name, name, nlen);
            bput(l, b, 1);
            ip.mtime = now(l);
            return lemon_iput(l, dino, &ip);
        }
        bput(l, b, 0);
    }
    return -ENOSPC;
}

int lemon_dir_remove(struct lemon *l, uint32_t dino, const char *name)
{
    struct lemon_inode ip;
    size_t nlen;
    uint64_t k, nblk;
    int r = dir_inode(l, dino, &ip);

    if (r < 0)
        return r;
    if ((r = name_ok(name, &nlen)) < 0)
        return r;
    nblk = ip.size / LEMON_BSIZE;
    for (k = 0; k < nblk; k++) {
        uint32_t b, j;
        uint8_t *p;

        if (bmap(l, &ip, k, 0, &b) < 0 || !b)
            continue;
        p = bget(l, b);
        for (j = 0; j < LEMON_DPB; j++) {
            struct lemon_dirent *de = (struct lemon_dirent *)(p + j * LEMON_DSIZE);

            if (de->ino && de->namelen == nlen && !memcmp(de->name, name, nlen)) {
                memset(de, 0, sizeof(*de));
                bput(l, b, 1);
                ip.mtime = now(l);
                return lemon_iput(l, dino, &ip);
            }
        }
        bput(l, b, 0);
    }
    return -ENOENT;
}

int lemon_dir_read(struct lemon *l, uint32_t dino, uint64_t idx, char *name, uint32_t *ino, int *type)
{
    struct lemon_inode ip;
    uint64_t k, nblk, seen = 0;
    int r = dir_inode(l, dino, &ip);

    if (r < 0)
        return r;
    nblk = ip.size / LEMON_BSIZE;
    for (k = 0; k < nblk; k++) {
        uint32_t b, j;
        uint8_t *p;

        if (bmap(l, &ip, k, 0, &b) < 0 || !b)
            continue;
        p = bget(l, b);
        for (j = 0; j < LEMON_DPB; j++) {
            struct lemon_dirent *de = (struct lemon_dirent *)(p + j * LEMON_DSIZE);

            if (!de->ino)
                continue;
            if (seen++ == idx) {
                memcpy(name, de->name, de->namelen);
                name[de->namelen] = 0;
                *ino = de->ino;
                *type = de->type;
                bput(l, b, 0);
                return 1;
            }
        }
        bput(l, b, 0);
    }
    return 0;
}

int lemon_dir_empty(struct lemon *l, uint32_t dino)
{
    char name[LEMON_NAME_MAX + 1];
    uint32_t ino;
    int type, r = lemon_dir_read(l, dino, 0, name, &ino, &type);

    return r < 0 ? r : r == 0;
}

int lemon_mkfs(struct blkdev *dev, uint32_t ninodes, const char *label)
{
    struct lemon l;
    struct lemon_sb *sb = &l.sb;
    struct lemon_inode root;
    uint32_t i, ino;
    uint8_t *p;

    memset(&l, 0, sizeof(l));
    l.dev = dev;
    ninodes = (ninodes + LEMON_IPB - 1) / LEMON_IPB * LEMON_IPB;
    if (dev->nblocks < 16 || ninodes < 16)
        return -EINVAL;

    memcpy(sb->magic, "LEMONFS1", 8);
    sb->version = 1;
    sb->bsize = LEMON_BSIZE;
    sb->nblocks = dev->nblocks;
    sb->ninodes = ninodes;
    sb->ibmap_blk = 1;
    sb->ibmap_n = (ninodes + BITS_PER_BLK - 1) / BITS_PER_BLK;
    sb->bbmap_blk = sb->ibmap_blk + sb->ibmap_n;
    sb->bbmap_n = (dev->nblocks + BITS_PER_BLK - 1) / BITS_PER_BLK;
    sb->itab_blk = sb->bbmap_blk + sb->bbmap_n;
    sb->itab_n = ninodes / LEMON_IPB;
    sb->data_blk = sb->itab_blk + sb->itab_n;
    if (sb->data_blk + 2 > dev->nblocks)
        return -EINVAL;
    sb->free_blocks = dev->nblocks - sb->data_blk;
    sb->free_inodes = ninodes - 1;
    sb->root_ino = LEMON_ROOT_INO;
    for (i = 0; i < sizeof(sb->label) - 1 && label && label[i]; i++)
        sb->label[i] = label[i];

    for (i = 1; i < sb->data_blk; i++) {
        p = dev->get(dev, i);
        memset(p, 0, LEMON_BSIZE);
        dev->put(dev, i, 1);
    }
    bm_set(&l, sb->ibmap_blk, 0, 1);
    for (i = 0; i < sb->data_blk; i++)
        bm_set(&l, sb->bbmap_blk, i, 1);
    sb_sync(&l);

    if (lemon_ialloc(&l, LEMON_IFDIR | 0755, &ino) < 0 || ino != LEMON_ROOT_INO)
        return -EIO;
    lemon_iget(&l, ino, &root);
    return 0;
}

int lemon_mount(struct lemon *l, struct blkdev *dev)
{
    uint8_t *p = dev->get(dev, 0);
    struct lemon_sb sb;

    memcpy(&sb, p, sizeof(sb));
    dev->put(dev, 0, 0);
    if (memcmp(sb.magic, "LEMONFS1", 8) || sb.bsize != LEMON_BSIZE || sb.nblocks != dev->nblocks)
        return -EINVAL;
    l->dev = dev;
    l->sb = sb;
    l->bhint = sb.data_blk;
    l->ihint = 1;
    return 0;
}

#define CHK(cond, ...) do { if (!(cond)) { bad++; if (l->log) l->log(__VA_ARGS__); } } while (0)

static void claim(struct lemon *l, uint8_t *seen, uint32_t blk, uint32_t ino, int *bad_out)
{
    int bad = 0;

    if (blk < l->sb.data_blk || blk >= l->sb.nblocks) {
        bad++;
        if (l->log)
            l->log("inode %u: block %u out of range\n", ino, blk);
    } else {
        CHK(bm_test(l, l->sb.bbmap_blk, blk), "inode %u: block %u not marked in bitmap\n", ino, blk);
        CHK(!seen[blk], "inode %u: block %u referenced twice\n", ino, blk);
        seen[blk] = 1;
    }
    *bad_out += bad;
}

int lemon_check(struct lemon *l, uint8_t *seen)
{
    uint32_t ino, b, nused = 0, ninodes_used = 0;
    int bad = 0;

    memset(seen, 0, l->sb.nblocks);
    for (ino = 1; ino < l->sb.ninodes; ino++) {
        struct lemon_inode ip;
        uint32_t i, j;

        if (!bm_test(l, l->sb.ibmap_blk, ino))
            continue;
        ninodes_used++;
        lemon_iget(l, ino, &ip);
        CHK((ip.mode & LEMON_IFMT) == LEMON_IFREG || (ip.mode & LEMON_IFMT) == LEMON_IFDIR,
            "inode %u: bad mode %o\n", ino, ip.mode);
        for (i = 0; i < LEMON_NDIRECT; i++)
            if (ip.direct[i])
                claim(l, seen, ip.direct[i], ino, &bad);
        if (ip.indirect) {
            uint32_t *p;

            claim(l, seen, ip.indirect, ino, &bad);
            p = (uint32_t *)bget(l, ip.indirect);
            for (i = 0; i < LEMON_PPB; i++)
                if (p[i])
                    claim(l, seen, p[i], ino, &bad);
            bput(l, ip.indirect, 0);
        }
        if (ip.dindirect) {
            claim(l, seen, ip.dindirect, ino, &bad);
            for (i = 0; i < LEMON_PPB; i++) {
                uint32_t *p = (uint32_t *)bget(l, ip.dindirect), mid = p[i];

                bput(l, ip.dindirect, 0);
                if (!mid)
                    continue;
                claim(l, seen, mid, ino, &bad);
                p = (uint32_t *)bget(l, mid);
                for (j = 0; j < LEMON_PPB; j++)
                    if (p[j])
                        claim(l, seen, p[j], ino, &bad);
                bput(l, mid, 0);
            }
        }
    }
    for (b = l->sb.data_blk; b < l->sb.nblocks; b++) {
        int used = bm_test(l, l->sb.bbmap_blk, b);

        if (used)
            nused++;
        CHK(!used || seen[b], "block %u allocated but unreferenced (leak)\n", b);
    }
    CHK(l->sb.free_blocks == l->sb.nblocks - l->sb.data_blk - nused,
        "free_blocks %u, expected %u\n", l->sb.free_blocks, l->sb.nblocks - l->sb.data_blk - nused);
    CHK(l->sb.free_inodes == l->sb.ninodes - 1 - ninodes_used,
        "free_inodes %u, expected %u\n", l->sb.free_inodes, l->sb.ninodes - 1 - ninodes_used);
    return bad;
}
