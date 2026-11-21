/* Host test: builds a root with mkroot() on an image file and reads it back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <lemonfs.h>
#include "../bin/install/src/mkroot.h"
#include "../bin/login/sha256.h"

struct fdev { int fd; uint64_t base; uint8_t buf[LEMON_BSIZE]; };

static uint8_t *f_get(struct blkdev *d, uint32_t b)
{
    struct fdev *f = d->priv;

    if (pread(f->fd, f->buf, LEMON_BSIZE, (off_t)(f->base + (uint64_t)b * LEMON_BSIZE)) != LEMON_BSIZE)
        memset(f->buf, 0, LEMON_BSIZE);
    return f->buf;
}

static void f_put(struct blkdev *d, uint32_t b, int dirty)
{
    (void)d; (void)b; (void)dirty;
}

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int hash_ok(const char *line, const char *pw)
{
    char salt[64], hash[80], hex[65], user[40];
    struct sha256 s;

    CHECK(sscanf(line, "%39[^:]:%63[^:]:%79[^:]:", user, salt, hash) == 3);
    sha256_init(&s);
    sha256_update(&s, salt, strlen(salt));
    sha256_update(&s, pw, strlen(pw));
    sha256_hex(&s, hex);
    return strcmp(hex, hash) ? 1 : 0;
}

int main(void)
{
    const uint64_t first = 2048 + 204800, nsect = 131072 - 33;
    struct account acc[2];
    struct sysconfig cfg;
    struct fdev f;
    struct blkdev dev;
    struct lemon l;
    struct lemon_inode ip;
    char db[1024];
    uint32_t ino, home, etc;
    int fd = open("/tmp/nirmala_mkroot.img", O_RDWR | O_CREAT | O_TRUNC, 0600);
    uint8_t *seen;

    CHECK(fd >= 0);
    CHECK(ftruncate(fd, (off_t)((first + nsect + 40) * 512)) == 0);
    memset(acc, 0, sizeof(acc));
    strcpy(acc[0].name, "root"); strcpy(acc[0].password, "rootpw"); strcpy(acc[0].home, "/root"); strcpy(acc[0].shell, "/bin/sh");
    strcpy(acc[1].name, "alice"); strcpy(acc[1].password, "s3cret pass"); strcpy(acc[1].home, "/home/alice");
    strcpy(acc[1].shell, "/bin/sh"); acc[1].uid = acc[1].gid = 1000;
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.keymap, "dvorak"); strcpy(cfg.locale, "id_ID.UTF-8"); strcpy(cfg.timezone, "Asia/Makassar");
    strcpy(cfg.hostname, "bali1"); cfg.net_static = 1;
    strcpy(cfg.ip, "10.0.2.50"); strcpy(cfg.mask, "255.255.255.0"); strcpy(cfg.gw, "10.0.2.2"); strcpy(cfg.dns, "10.0.2.3");
    CHECK(mkroot(fd, first, nsect, acc, 2, &cfg) == 0);

    f.fd = fd; f.base = first * 512;
    dev.nblocks = (uint32_t)(nsect / 8); dev.priv = &f; dev.get = f_get; dev.put = f_put;
    memset(&l, 0, sizeof(l));
    CHECK(lemon_mount(&l, &dev) == 0);
    seen = malloc(dev.nblocks);
    CHECK(lemon_check(&l, seen) == 0);
    free(seen);
    CHECK(lemon_dir_lookup(&l, LEMON_ROOT_INO, "etc", &etc, NULL) == 0);
    {
        static const char *const names[] = { "keymap", "locale", "timezone", "hostname", "network" };
        static const char *const want[] = { "dvorak\n", "id_ID.UTF-8\n", "Asia/Makassar\n", "bali1\n",
                                            "static 10.0.2.50 255.255.255.0 10.0.2.2 10.0.2.3\n" };
        unsigned k;

        for (k = 0; k < 5; k++) {
            char got[128];
            long n;

            CHECK(lemon_dir_lookup(&l, etc, names[k], &ino, NULL) == 0);
            memset(got, 0, sizeof(got));
            n = lemon_read(&l, ino, 0, got, sizeof(got) - 1);
            CHECK(n == (long)strlen(want[k]) && !strcmp(got, want[k]));
        }
    }
    CHECK(lemon_dir_lookup(&l, etc, "passwd", &ino, NULL) == 0);
    CHECK(lemon_iget(&l, ino, &ip) == 0 && (ip.mode & 07777) == 0600 && ip.uid == 0);
    memset(db, 0, sizeof(db));
    CHECK(lemon_read(&l, ino, 0, db, sizeof(db) - 1) > 0);
    printf("%s", db);
    {
        char *nl = strchr(db, '\n');

        CHECK(nl);
        *nl = 0;
        CHECK(!hash_ok(db, "rootpw"));
        CHECK(!hash_ok(nl + 1, "s3cret pass"));
        CHECK(hash_ok(nl + 1, "wrong"));
        CHECK(strstr(nl + 1, ":1000:1000:/home/alice:/bin/sh"));
    }
    CHECK(lemon_dir_lookup(&l, LEMON_ROOT_INO, "home", &home, NULL) == 0);
    CHECK(lemon_dir_lookup(&l, home, "alice", &ino, NULL) == 0);
    CHECK(lemon_iget(&l, ino, &ip) == 0);
    CHECK((ip.mode & LEMON_IFMT) == LEMON_IFDIR && ip.uid == 1000 && ip.gid == 1000 && (ip.mode & 07777) == 0700);
    CHECK(lemon_dir_lookup(&l, LEMON_ROOT_INO, "root", &ino, NULL) == 0);
    CHECK(lemon_iget(&l, ino, &ip) == 0 && (ip.mode & 07777) == 0700);
    CHECK(lemon_dir_lookup(&l, LEMON_ROOT_INO, "tmp", &ino, NULL) == 0);
    CHECK(lemon_iget(&l, ino, &ip) == 0 && (ip.mode & 07777) == 01777);
    close(fd);
    unlink("/tmp/nirmala_mkroot.img");
    printf("mkroot_test: ok (%u blocks)\n", dev.nblocks);
    return 0;
}
