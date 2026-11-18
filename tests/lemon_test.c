#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <lemonfs.h>
#include <sys/errno.h>

static uint8_t *disk;
static int held, maxheld;

static uint8_t *rd_get(struct blkdev *d, uint32_t blk)
{
    if (blk >= d->nblocks) { printf("FAIL: get beyond device %u\n", blk); abort(); }
    if (++held > maxheld) maxheld = held;
    return disk + (size_t)blk * LEMON_BSIZE;
}
static void rd_put(struct blkdev *d, uint32_t blk, int dirty) { (void)d; (void)blk; (void)dirty; held--; }

static void logf_(const char *fmt, ...) { va_list ap; va_start(ap, fmt); printf("  CHECK: "); vprintf(fmt, ap); va_end(ap); }

static struct blkdev dev;
static struct lemon L;
static int fails;
#define T(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)

static void fsck(const char *when)
{
    uint8_t *scratch = malloc(dev.nblocks);
    int bad = lemon_check(&L, scratch);
    free(scratch);
    if (bad || held) { printf("FAIL: fsck after %s: %d problems, held=%d\n", when, bad, held); fails++; }
}

static uint32_t mkfile(uint32_t dir, const char *name)
{
    uint32_t ino;
    T(lemon_ialloc(&L, LEMON_IFREG | 0644, &ino) == 0, "ialloc");
    T(lemon_dir_add(&L, dir, name, ino, LEMON_DT_REG) == 0, "dir_add");
    return ino;
}

static void rmfile(uint32_t dir, const char *name, uint32_t ino)
{
    T(lemon_dir_remove(&L, dir, name) == 0, "dir_remove");
    T(lemon_ifree(&L, ino) == 0, "ifree");
}

int main(void)
{
    uint32_t nblocks = 2048;
    disk = calloc(nblocks, LEMON_BSIZE);
    dev.nblocks = nblocks; dev.get = rd_get; dev.put = rd_put;
    T(lemon_mkfs(&dev, 256, "test") == 0, "mkfs");
    T(lemon_mount(&L, &dev) == 0, "mount");
    L.log = logf_;
    uint32_t free0 = L.sb.free_blocks;
    fsck("mkfs");
    T(lemon_dir_empty(&L, LEMON_ROOT_INO) == 1, "root empty");

    uint32_t a = mkfile(LEMON_ROOT_INO, "hello");
    T(lemon_write(&L, a, 0, "hello world", 11) == 11, "write");
    char buf[64] = {0};
    T(lemon_read(&L, a, 0, buf, 63) == 11 && !strcmp(buf, "hello world"), "read back");
    T(lemon_read(&L, a, 6, buf, 63) == 5 && !memcmp(buf, "world", 5), "read offset");
    T(lemon_read(&L, a, 11, buf, 10) == 0, "read at EOF");
    uint32_t ino; int type;
    T(lemon_dir_lookup(&L, LEMON_ROOT_INO, "hello", &ino, &type) == 0 && ino == a && type == LEMON_DT_REG, "lookup");
    T(lemon_dir_lookup(&L, LEMON_ROOT_INO, "nope", &ino, &type) == -ENOENT, "lookup miss");
    char longname[100]; memset(longname, 'x', 99); longname[99] = 0;
    T(lemon_dir_lookup(&L, LEMON_ROOT_INO, longname, &ino, &type) == -ENAMETOOLONG, "name too long");
    T(lemon_dir_lookup(&L, a, "x", &ino, &type) == -ENOTDIR, "lookup in file");
    fsck("basic");

    uint32_t big = mkfile(LEMON_ROOT_INO, "big");
    size_t BIG = 5u * 1024 * 1024 + 123;
    uint8_t *pat = malloc(BIG), *chk = malloc(BIG);
    for (size_t i = 0; i < BIG; i++) pat[i] = (uint8_t)(i * 31 + (i >> 12));
    size_t off = 0;
    while (off < BIG) { size_t n = 7000; if (n > BIG - off) n = BIG - off; long r = lemon_write(&L, big, off, pat + off, n); T(r == (long)n, "big write"); if (r <= 0) break; off += (size_t)r; }
    T(lemon_read(&L, big, 0, chk, BIG) == (long)BIG && !memcmp(pat, chk, BIG), "big read back");
    fsck("big write");
    T(lemon_truncate(&L, big, 100000) == 0, "truncate shrink");
    memset(chk, 0, BIG);
    T(lemon_read(&L, big, 0, chk, BIG) == 100000 && !memcmp(pat, chk, 100000), "data after shrink");
    fsck("shrink");
    T(lemon_truncate(&L, big, 300000) == 0, "truncate grow");
    T(lemon_read(&L, big, 0, chk, 300000) == 300000, "read grown");
    int allzero = 1; for (size_t i = 100000; i < 300000; i++) if (chk[i]) allzero = 0;
    T(allzero, "grown region is zero");
    T(lemon_truncate(&L, big, 0) == 0, "truncate 0");
    fsck("truncate 0");
    T(L.sb.free_blocks + 2 >= free0 - 1, "blocks returned");
    free(pat); free(chk);

    uint32_t sp = mkfile(LEMON_ROOT_INO, "sparse");
    uint64_t far = (uint64_t)(LEMON_NDIRECT + LEMON_PPB + 5) * LEMON_BSIZE;
    T(lemon_write(&L, sp, far, "Z", 1) == 1, "sparse write");
    T(lemon_read(&L, sp, far, buf, 1) == 1 && buf[0] == 'Z', "sparse read");
    T(lemon_read(&L, sp, far - 10, buf, 10) == 10 && !memcmp(buf, "\0\0\0\0\0\0\0\0\0\0", 10), "hole reads zero");
    fsck("sparse");
    rmfile(LEMON_ROOT_INO, "sparse", sp);
    fsck("sparse removed");

    char name[32];
    uint32_t inos[200];
    for (int i = 0; i < 200; i++) { snprintf(name, sizeof name, "file%03d", i); inos[i] = mkfile(LEMON_ROOT_INO, name); if (!inos[i]) break; }
    T(L.sb.free_inodes < 60, "inode table fills up");
    fsck("many files");
    int count = 0; char nm[LEMON_NAME_MAX + 1];
    while (lemon_dir_read(&L, LEMON_ROOT_INO, (uint64_t)count, nm, &ino, &type) == 1) count++;
    T(count == 3 + 200 - 0 || count >= 100, "readdir count");
    uint32_t extra;
    T(lemon_ialloc(&L, LEMON_IFREG, &extra) == -ENOSPC || lemon_ialloc(&L, LEMON_IFREG, &extra) == 0, "ialloc at limit sane");
    for (int i = 0; i < 200; i += 2) { snprintf(name, sizeof name, "file%03d", i); if (lemon_dir_lookup(&L, LEMON_ROOT_INO, name, &ino, &type) == 0) rmfile(LEMON_ROOT_INO, name, ino); }
    fsck("half removed");
    for (int i = 1; i < 200; i += 2) { snprintf(name, sizeof name, "file%03d", i); if (lemon_dir_lookup(&L, LEMON_ROOT_INO, name, &ino, &type) == 0) rmfile(LEMON_ROOT_INO, name, ino); }
    fsck("all removed");

    uint32_t sub;
    T(lemon_ialloc(&L, LEMON_IFDIR | 0755, &sub) == 0, "dir ialloc");
    T(lemon_dir_add(&L, LEMON_ROOT_INO, "sub", sub, LEMON_DT_DIR) == 0, "dir add");
    uint32_t inner = mkfile(sub, "inner");
    T(lemon_dir_empty(&L, sub) == 0, "sub not empty");
    T(lemon_dir_lookup(&L, sub, "inner", &ino, &type) == 0 && ino == inner, "lookup in sub");
    rmfile(sub, "inner", inner);
    T(lemon_dir_empty(&L, sub) == 1, "sub empty again");
    T(lemon_dir_remove(&L, LEMON_ROOT_INO, "sub") == 0 && lemon_ifree(&L, sub) == 0, "remove sub");
    fsck("subdir");

    uint32_t fill = mkfile(LEMON_ROOT_INO, "fill");
    uint8_t blk[4096]; memset(blk, 0xAA, sizeof blk);
    long r; uint64_t o = 0, total = 0;
    while ((r = lemon_write(&L, fill, o, blk, sizeof blk)) > 0) { o += (uint64_t)r; total += (uint64_t)r; }
    T(r == -ENOSPC, "write reports ENOSPC");
    T(L.sb.free_blocks == 0, "disk full");
    fsck("disk full");
    T(lemon_truncate(&L, fill, 0) == 0, "truncate full file");
    T(lemon_write(&L, fill, 0, blk, sizeof blk) == (long)sizeof blk, "write works again");
    rmfile(LEMON_ROOT_INO, "fill", fill);
    rmfile(LEMON_ROOT_INO, "hello", a);
    rmfile(LEMON_ROOT_INO, "big", big);
    fsck("cleanup");
    struct lemon_inode rootip; lemon_iget(&L, LEMON_ROOT_INO, &rootip);
    uint32_t dirblocks = (uint32_t)(rootip.size / LEMON_BSIZE);
    T(L.sb.free_blocks == free0 - dirblocks, "all file blocks free again (root dir keeps its blocks)");
    printf("  disk filled with %llu KiB before ENOSPC, max blocks held at once: %d\n", (unsigned long long)(total / 1024), maxheld);

    srand(1234);
    enum { NF = 8, MAXSZ = 300000 };
    static uint8_t model[NF][MAXSZ]; size_t msz[NF] = {0}; uint32_t fi[NF];
    for (int i = 0; i < NF; i++) { snprintf(name, sizeof name, "r%d", i); fi[i] = mkfile(LEMON_ROOT_INO, name); }
    for (int iter = 0; iter < 4000; iter++) {
        int f = rand() % NF; int op = rand() % 10;
        if (op < 5) {
            size_t o2 = (size_t)(rand() % (MAXSZ - 9000)), n = (size_t)(rand() % 9000) + 1; uint8_t d[9000];
            for (size_t k = 0; k < n; k++) d[k] = (uint8_t)rand();
            long w = lemon_write(&L, fi[f], o2, d, n);
            if (w == (long)n) { if (o2 > msz[f]) memset(model[f] + msz[f], 0, o2 - msz[f]); memcpy(model[f] + o2, d, n); if (o2 + n > msz[f]) msz[f] = o2 + n; }
            else if (w < 0) { T(w == -ENOSPC, "write error is ENOSPC"); if (w < 0) { struct lemon_inode ip; lemon_iget(&L, fi[f], &ip); msz[f] = ip.size; lemon_read(&L, fi[f], 0, model[f], msz[f]); } }
            else { struct lemon_inode ip; lemon_iget(&L, fi[f], &ip); if (w > 0) { if (o2 > msz[f]) memset(model[f] + msz[f], 0, o2 - msz[f]); memcpy(model[f] + o2, d, (size_t)w); } msz[f] = ip.size; }
        } else if (op < 7) {
            size_t ns = (size_t)(rand() % MAXSZ);
            if (lemon_truncate(&L, fi[f], ns) == 0) { if (ns > msz[f]) memset(model[f] + msz[f], 0, ns - msz[f]); msz[f] = ns; }
        } else {
            static uint8_t got[MAXSZ];
            long n = lemon_read(&L, fi[f], 0, got, MAXSZ);
            if (n != (long)msz[f] || memcmp(got, model[f], msz[f])) { printf("FAIL: model mismatch iter %d file %d (got %ld want %zu)\n", iter, f, n, msz[f]); fails++; break; }
        }
        if (iter % 500 == 0) fsck("random ops");
    }
    for (int i = 0; i < NF; i++) { snprintf(name, sizeof name, "r%d", i); rmfile(LEMON_ROOT_INO, name, fi[i]); }
    fsck("random cleanup");
    T(L.sb.free_blocks == free0 - dirblocks, "no leaks after random ops");

    struct lemon L2; T(lemon_mount(&L2, &dev) == 0 && L2.sb.free_blocks == L.sb.free_blocks, "remount");
    printf(fails ? "lemonfs: %d FAILURES\n" : "lemonfs: all tests passed\n", fails);
    return fails != 0;
}
