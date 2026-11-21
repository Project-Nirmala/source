#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <lemonfs.h>
#include <diskfmt.h>
#include "mkroot.h"
#include "../../login/sha256.h"

#define NBUF 64
#define MIN_INODES 1024u
#define MAX_INODES 65536u

struct ubuf {
    uint32_t blk, holds;
    uint64_t stamp;
    int      valid;
    uint8_t  data[LEMON_BSIZE];
};

struct udev {
    int          fd;
    uint64_t     base;
    uint64_t     clock;
    int          err;
    struct ubuf *bufs;
};

static int block_io(struct udev *u, uint32_t blk, void *buf, int wr)
{
    uint8_t *p = buf;
    size_t left = LEMON_BSIZE;

    if (lseek(u->fd, (off_t)(u->base + (uint64_t)blk * LEMON_BSIZE), SEEK_SET) < 0)
        return -1;
    while (left) {
        long r = wr ? write(u->fd, p, left) : read(u->fd, p, left);

        if (r <= 0)
            return -1;
        p += r;
        left -= (size_t)r;
    }
    return 0;
}

static uint8_t *u_get(struct blkdev *dev, uint32_t blk)
{
    struct udev *u = dev->priv;
    struct ubuf *victim = NULL;
    unsigned i;

    if (blk >= dev->nblocks) {
        dprintf(2, "install: block %u is beyond the end of the partition\n", blk);
        exit(1);
    }
    for (i = 0; i < NBUF; i++) {
        struct ubuf *b = &u->bufs[i];

        if (b->valid && b->blk == blk) {
            b->holds++;
            b->stamp = ++u->clock;
            return b->data;
        }
    }
    for (i = 0; i < NBUF; i++) {
        struct ubuf *b = &u->bufs[i];

        if (b->holds)
            continue;
        if (!b->valid) {
            victim = b;
            break;
        }
        if (!victim || b->stamp < victim->stamp)
            victim = b;
    }
    if (!victim) {
        dprintf(2, "install: every block buffer is in use\n");
        exit(1);
    }
    victim->valid = 0;
    if (block_io(u, blk, victim->data, 0) < 0) {
        u->err = 1;
        memset(victim->data, 0, LEMON_BSIZE);
    }
    victim->blk = blk;
    victim->valid = 1;
    victim->holds = 1;
    victim->stamp = ++u->clock;
    return victim->data;
}

static void u_put(struct blkdev *dev, uint32_t blk, int dirty)
{
    struct udev *u = dev->priv;
    unsigned i;

    for (i = 0; i < NBUF; i++) {
        struct ubuf *b = &u->bufs[i];

        if (b->valid && b->blk == blk && b->holds) {
            b->holds--;
            if (dirty && block_io(u, blk, b->data, 1) < 0)
                u->err = 1;
            return;
        }
    }
    u->err = 1;
}

static uint32_t now_secs(void)
{
    return (uint32_t)time(NULL);
}

static int make_node(struct lemon *l, uint32_t parent, const char *name, int isdir, unsigned mode,
                     uint32_t uid, uint32_t gid, uint32_t *out)
{
    struct lemon_inode ip;
    uint32_t ino;
    int r = lemon_dir_lookup(l, parent, name, &ino, NULL);

    if (r == 0) {
        *out = ino;
        return 0;
    }
    r = lemon_ialloc(l, (uint16_t)((isdir ? LEMON_IFDIR : LEMON_IFREG) | (mode & 07777)), &ino);
    if (r < 0)
        return r;
    r = lemon_dir_add(l, parent, name, ino, isdir ? LEMON_DT_DIR : LEMON_DT_REG);
    if (r < 0) {
        lemon_ifree(l, ino);
        return r;
    }
    if (lemon_iget(l, ino, &ip) == 0) {
        ip.uid = uid;
        ip.gid = gid;
        lemon_iput(l, ino, &ip);
    }
    *out = ino;
    return 0;
}

static int make_dir(struct lemon *l, const char *path, unsigned mode, uint32_t uid, uint32_t gid)
{
    char part[LEMON_NAME_MAX + 1];
    uint32_t dir = LEMON_ROOT_INO;
    const char *p = path;

    while (*p) {
        size_t n = 0;

        while (*p == '/')
            p++;
        if (!*p)
            break;
        while (p[n] && p[n] != '/')
            n++;
        if (n > LEMON_NAME_MAX)
            return -1;
        memcpy(part, p, n);
        part[n] = 0;
        p += n;
        if (make_node(l, dir, part, 1, *p ? 0755 : mode, uid, gid, &dir) < 0)
            return -1;
    }
    return 0;
}

static void to_hex(const uint8_t *in, size_t n, char *out)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < n; i++) {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 15];
    }
    out[2 * n] = 0;
}

static int passwd_line(const struct account *a, char *out, size_t size)
{
    uint8_t rnd[16];
    char salt[17], hex[65];
    struct sha256 s;
    int n;

    gpt_random_guid(rnd);
    to_hex(rnd, 8, salt);
    sha256_init(&s);
    sha256_update(&s, salt, strlen(salt));
    sha256_update(&s, a->password, strlen(a->password));
    sha256_hex(&s, hex);
    n = snprintf(out, size, "%s:%s:%s:%u:%u:%s:%s\n", a->name, salt, hex, (unsigned)a->uid, (unsigned)a->gid,
                 a->home, a->shell);
    return n > 0 && (size_t)n < size ? n : -1;
}

static int write_file(struct lemon *l, const char *dirpath, const char *name, unsigned mode, const void *data,
                      size_t len)
{
    uint32_t dir = LEMON_ROOT_INO, ino;
    char part[LEMON_NAME_MAX + 1];
    const char *p = dirpath;

    while (*p) {
        size_t n = 0;

        while (*p == '/')
            p++;
        if (!*p)
            break;
        while (p[n] && p[n] != '/')
            n++;
        memcpy(part, p, n);
        part[n] = 0;
        p += n;
        if (lemon_dir_lookup(l, dir, part, &dir, NULL) < 0)
            return -1;
    }
    if (make_node(l, dir, name, 0, mode, 0, 0, &ino) < 0)
        return -1;
    return lemon_write(l, ino, 0, data, len) == (long)len ? 0 : -1;
}

static int write_line(struct lemon *l, const char *name, const char *value)
{
    char line[96];
    int n = snprintf(line, sizeof(line), "%s\n", value);

    if (n < 2 || (size_t)n >= sizeof(line))
        return -1;
    return write_file(l, "/etc", name, 0644, line, (size_t)n);
}

static int write_settings(struct lemon *l, const struct sysconfig *c)
{
    char net[96];

    if (c->keymap[0] && write_line(l, "keymap", c->keymap) < 0)
        return -1;
    if (c->locale[0] && write_line(l, "locale", c->locale) < 0)
        return -1;
    if (c->timezone[0] && write_line(l, "timezone", c->timezone) < 0)
        return -1;
    if (c->hostname[0] && write_line(l, "hostname", c->hostname) < 0)
        return -1;
    if (c->net_static)
        snprintf(net, sizeof(net), "static %s %s %s %s", c->ip, c->mask, c->gw, c->dns);
    else
        snprintf(net, sizeof(net), "dhcp");
    return write_line(l, "network", net);
}

int mkroot(int fd, uint64_t first, uint64_t nsect, const struct account *acc, unsigned nacc,
           const struct sysconfig *cfg)
{
    struct udev *u = calloc(1, sizeof(*u));
    struct blkdev dev;
    struct lemon l;
    char db[1024];
    size_t dblen = 0;
    uint8_t *seen;
    uint32_t nblocks = (uint32_t)(nsect / (LEMON_BSIZE / 512)), ninodes;
    unsigned i;
    int bad, ok = -1;

    if (!u || nacc == 0 || nacc > MKROOT_MAX_ACCOUNTS)
        goto out;
    u->bufs = calloc(NBUF, sizeof(struct ubuf));
    if (!u->bufs)
        goto out;
    u->fd = fd;
    u->base = first * 512;
    memset(&dev, 0, sizeof(dev));
    dev.nblocks = nblocks;
    dev.priv = u;
    dev.get = u_get;
    dev.put = u_put;

    ninodes = nblocks / 4;
    if (ninodes < MIN_INODES)
        ninodes = MIN_INODES;
    if (ninodes > MAX_INODES)
        ninodes = MAX_INODES;
    if (lemon_mkfs(&dev, ninodes, "nirmala") < 0 || u->err) {
        dprintf(2, "install: cannot create the root filesystem\n");
        goto out;
    }
    memset(&l, 0, sizeof(l));
    if (lemon_mount(&l, &dev) < 0) {
        dprintf(2, "install: cannot mount the new root filesystem\n");
        goto out;
    }
    l.now = now_secs;

    if (make_dir(&l, "/etc", 0755, 0, 0) < 0 || make_dir(&l, "/tmp", 01777, 0, 0) < 0 ||
        make_dir(&l, "/root", 0700, 0, 0) < 0 || make_dir(&l, "/home", 0755, 0, 0) < 0) {
        dprintf(2, "install: cannot create the base directories\n");
        goto out;
    }
    for (i = 0; i < nacc; i++) {
        int n = passwd_line(&acc[i], db + dblen, sizeof(db) - dblen);

        if (n < 0) {
            dprintf(2, "install: too many accounts for /etc/passwd\n");
            goto out;
        }
        dblen += (size_t)n;
        if (strcmp(acc[i].home, "/root") && make_dir(&l, acc[i].home, 0700, acc[i].uid, acc[i].gid) < 0) {
            dprintf(2, "install: cannot create %s\n", acc[i].home);
            goto out;
        }
    }
    if (write_file(&l, "/etc", "passwd", 0600, db, dblen) < 0) {
        dprintf(2, "install: cannot write /etc/passwd\n");
        goto out;
    }
    if (cfg && write_settings(&l, cfg) < 0) {
        dprintf(2, "install: cannot write the system settings\n");
        goto out;
    }

    seen = malloc(nblocks);
    bad = seen ? lemon_check(&l, seen) : 0;
    free(seen);
    if (bad || u->err) {
        dprintf(2, "install: the new root filesystem failed its consistency check\n");
        goto out;
    }
    ok = 0;
out:
    if (u)
        free(u->bufs);
    free(u);
    return ok;
}
