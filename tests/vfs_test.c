#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <vfs.h>
#include <blk.h>
#include <sys/errno.h>

void *kmalloc(size_t n) { return malloc(n); }
void *kzalloc(size_t n) { return calloc(1, n); }
void kfree(void *p) { free(p); }
void kprintf(const char *f, ...) { va_list ap; va_start(ap, f); printf("  [kernel] "); vprintf(f, ap); va_end(ap); }
void panic(const char *f, ...) { va_list ap; va_start(ap, f); printf("PANIC: "); vprintf(f, ap); va_end(ap); abort(); }
uint64_t pit_ticks(void) { return 1000; }
uint64_t wall_seconds(void) { return 1700000000; }
uint32_t pit_hz(void) { return 100; }
struct vnode console_vnode;
struct blkdrv *virtio_blk_probe(void) { return NULL; }

static int fails;
#define T(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)

static void *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc(*n + 1); if (fread(b, 1, *n, f) != *n) { fclose(f); return NULL; } fclose(f); return b;
}

static struct vnode *R;

static long wr(const char *path, const char *s)
{
    struct vnode *v; int r = vfs_create(path, R, 0644, 0, &v);
    if (r < 0) return r;
    return vnode_write(v, 0, s, strlen(s));
}
static int rd(const char *path, char *buf, size_t n)
{
    struct vnode *v; int r = vfs_lookup(path, R, &v); if (r < 0) return r;
    long k = vnode_read(v, 0, buf, n - 1); if (k < 0) return (int)k; buf[k] = 0; return (int)k;
}
static int exists(const char *p) { struct vnode *v; return vfs_lookup(p, R, &v) == 0; }

int main(void)
{
    size_t tsz; void *tar = slurp("build/initrd.tar", &tsz);
    if (!tar) { printf("need build/initrd.tar (run make)\n"); return 2; }
    vfs_init(tar, tsz);
    R = vfs_root();
    T(R && R->type == VDIR, "root");

    char cmd[256]; FILE *p = popen("cd build/rootfs && find . -type f | sed 's|^\\./||'", "r"); char line[256]; int nfiles = 0;
    while (fgets(line, sizeof line, p)) {
        line[strcspn(line, "\n")] = 0;
        char host[300], vp[300]; snprintf(host, sizeof host, "build/rootfs/%s", line); snprintf(vp, sizeof vp, "/%s", line);
        size_t hn; void *hd = slurp(host, &hn); struct vnode *v;
        if (vfs_lookup(vp, R, &v) < 0) { printf("FAIL: %s missing in vfs\n", vp); fails++; continue; }
        T(v->size == hn, "size matches");
        void *vd = malloc(hn + 1); long got = vnode_read(v, 0, vd, hn);
        if (got != (long)hn || memcmp(vd, hd, hn)) { printf("FAIL: content mismatch %s\n", vp); fails++; }
        free(vd); free(hd); nfiles++;
    }
    pclose(p); (void)cmd;
    printf("  %d initrd files verified byte-for-byte\n", nfiles);
    T(nfiles >= 8, "found initrd files");

    char buf[256];
    T(wr("/hello.txt", "hi there") == 8, "create+write");
    T(rd("/hello.txt", buf, sizeof buf) == 8 && !strcmp(buf, "hi there"), "read back");
    T(vfs_mkdir("/work", R, 0755) == 0, "mkdir");
    T(vfs_mkdir("/work", R, 0755) == -EEXIST, "mkdir exists");
    T(vfs_mkdir("/nope/deeper", R, 0755) == -ENOENT, "mkdir missing parent");
    T(vfs_mkdir("/hello.txt/x", R, 0755) == -ENOTDIR, "mkdir under file");
    T(wr("/work/a", "AAA") == 3 && wr("/work/b", "BBBB") == 4, "files in subdir");
    struct vnode *w; T(vfs_lookup("/work", R, &w) == 0, "lookup subdir");
    T(rd("a", buf, sizeof buf) == -ENOENT, "relative from root does not see work/a");
    { struct vnode *v; T(vfs_lookup("a", w, &v) == 0 && v->parent == w, "relative lookup with cwd"); char pth[64]; T(vfs_getpath(v, pth, sizeof pth) > 0 && !strcmp(pth, "/work/a"), "getpath"); }
    { struct vnode *v; T(vfs_lookup("../hello.txt", w, &v) == 0, "dotdot"); T(vfs_lookup("/work/../work/./a", R, &v) == 0, "dot and dotdot mid-path"); }

    { struct vnode *e; char nm[NAME_MAX + 1]; int n = 0, seen_a = 0, seen_dot = 0;
      while (vfs_readdir(w, (uint64_t)n, &e, nm)) { if (!strcmp(nm, "a")) seen_a = 1; if (!strcmp(nm, ".")) seen_dot = 1; n++; }
      T(n == 4 && seen_a && seen_dot, "readdir lists . .. a b"); }

    { struct vnode *v; vfs_lookup("/work/a", R, &v); T(vfs_truncate(v, 1) == 0 && v->size == 1, "truncate"); T(rd("/work/a", buf, sizeof buf) == 1 && buf[0] == 'A', "after truncate");
      T(vnode_write(v, 1, "ZZ", 2) == 2 && v->size == 3, "write extends size"); T(vfs_truncate(w, 0) == -EISDIR, "truncate dir"); }

    T(vfs_rename("/work/a", "/work/c", R) == 0 && !exists("/work/a") && exists("/work/c"), "rename in dir");
    { struct vnode *v; vfs_lookup("/work/c", R, &v); char pth[64]; T(vfs_getpath(v, pth, sizeof pth) > 0 && !strcmp(pth, "/work/c"), "path after rename"); }
    T(vfs_mkdir("/dst", R, 0755) == 0 && vfs_rename("/work/c", "/dst/moved", R) == 0 && exists("/dst/moved"), "move across dirs");
    { struct vnode *v; vfs_lookup("/dst/moved", R, &v); char pth[64]; T(vfs_getpath(v, pth, sizeof pth) > 0 && !strcmp(pth, "/dst/moved"), "path after move"); }
    T(wr("/x1", "one") == 3 && wr("/x2", "two") == 3, "x1 x2");
    T(vfs_rename("/x1", "/x2", R) == 0 && rd("/x2", buf, sizeof buf) == 3 && !strcmp(buf, "one") && !exists("/x1"), "rename overwrites");
    T(vfs_rename("/work", "/work/inside", R) == -EINVAL, "no dir into itself");
    T(vfs_rename("/x2", "/dst", R) == -EISDIR, "file over dir");
    T(vfs_rename("/dst", "/x2", R) == -ENOTDIR, "dir over file");
    T(vfs_rename("/dst", "/work", R) == -ENOTEMPTY, "dir over non-empty dir");
    T(vfs_rename("/nothing", "/y", R) == -ENOENT, "rename missing");

    T(vfs_unlink("/work", R) == -EISDIR, "unlink dir");
    T(vfs_rmdir("/x2", R) == -ENOTDIR, "rmdir file");
    T(vfs_rmdir("/work", R) == -ENOTEMPTY, "rmdir non-empty");
    T(vfs_unlink("/work/b", R) == 0 && vfs_rmdir("/work", R) == 0 && !exists("/work"), "remove file then dir");
    T(vfs_unlink("/nothing", R) == -ENOENT, "unlink missing");
    T(vfs_rmdir("/", R) == -EBUSY, "rmdir root");

    { struct vnode *v; wr("/tmpfile", "still here"); vfs_lookup("/tmpfile", R, &v);
      v->refs++;
      T(vfs_unlink("/tmpfile", R) == 0 && !exists("/tmpfile"), "unlinked while open");
      char b2[32]; long k = vnode_read(v, 0, b2, 31); T(k == 10 && !memcmp(b2, "still here", 10), "read after unlink");
      struct vstatfs a, b; vfs_statfs(R, &a); v->refs--; v->ops->release(v); vfs_statfs(R, &b);
      T(b.bfree == a.bfree + 1 && b.ffree == a.ffree + 1, "space reclaimed on last close"); }

    { struct vnode *d; vfs_mkdir("/gone", R, 0755); vfs_lookup("/gone", R, &d); T(vfs_rmdir("/gone", R) == 0, "rmdir");
      struct vnode *v; T(vfs_lookup("anything", d, &v) < 0, "lookup in removed dir fails"); T(vfs_mkdir("/gone2", R, 0755) == 0, "ino reuse ok"); T(vfs_lookup("/gone2", R, &v) == 0 && v != d, "reused inode gets a fresh vnode"); vfs_rmdir("/gone2", R); }

    { struct vnode *v; T(vfs_create("/hello.txt", R, 0644, 1, &v) == -EEXIST, "O_EXCL"); T(vfs_create("/dst", R, 0644, 0, &v) == -EISDIR, "create over dir");
      char longn[80]; memset(longn, 'n', 79); longn[79] = 0; char lp[100]; snprintf(lp, sizeof lp, "/%s", longn); T(vfs_create(lp, R, 0644, 0, &v) == -ENAMETOOLONG, "name too long"); }

    { struct vnode *v; char nm[64]; int made = 0;
      vfs_mkdir("/many", R, 0755);
      for (int i = 0; i < 300; i++) { snprintf(nm, sizeof nm, "/many/f%d", i); if (vfs_create(nm, R, 0644, 0, &v) < 0) break; vnode_write(v, 0, nm, strlen(nm)); made++; }
      T(made == 300, "300 files in a directory");
      int ok = 1; for (int i = 0; i < 300; i++) { snprintf(nm, sizeof nm, "/many/f%d", i); char b2[64]; if (rd(nm, b2, sizeof b2) != (int)strlen(nm) || strcmp(b2, nm)) ok = 0; }
      T(ok, "all 300 read back");
      for (int i = 0; i < 300; i++) { snprintf(nm, sizeof nm, "/many/f%d", i); vfs_unlink(nm, R); }
      T(vfs_rmdir("/many", R) == 0, "many removed");
      struct vstatfs s0, s1; vfs_statfs(R, &s0);
      vfs_create("/fill", R, 0644, 0, &v); static char blk[4096]; long r; uint64_t o = 0;
      while ((r = vnode_write(v, o, blk, sizeof blk)) > 0) o += (uint64_t)r;
      T(r == -ENOSPC, "ENOSPC through the VFS");
      T(vfs_unlink("/fill", R) == 0, "delete the big file");
      vfs_statfs(R, &s1); T(s1.bfree == s0.bfree, "space fully reclaimed"); }

    printf(fails ? "vfs: %d FAILURES\n" : "vfs: all tests passed\n", fails);
    return fails != 0;
}
