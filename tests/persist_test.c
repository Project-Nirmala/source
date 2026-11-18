#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <blk.h>
#include <vfs.h>
#include <sys/errno.h>

void *kmalloc(size_t n) { return malloc(n); }
void *kzalloc(size_t n) { return calloc(1, n); }
void kfree(void *p) { free(p); }
static char klog[4096];
void kprintf(const char *f, ...) { va_list ap; char b[256]; va_start(ap, f); vsnprintf(b, sizeof b, f, ap); va_end(ap); strncat(klog, b, sizeof klog - strlen(klog) - 1); }
void panic(const char *f, ...) { va_list ap; va_start(ap, f); printf("PANIC: "); vprintf(f, ap); va_end(ap); abort(); }
uint64_t pit_ticks(void) { return 1000; }
uint64_t wall_seconds(void) { return 1700000000; }
uint32_t pit_hz(void) { return 100; }
struct vnode console_vnode;

#define NBLK 2048
static uint8_t *diskmem;
static struct blkdrv drv;
static int have_disk;
static int mread(void *p, uint32_t b, void *buf) { (void)p; memcpy(buf, diskmem + (size_t)b * 4096, 4096); return 0; }
static int mwrite(void *p, uint32_t b, const void *buf) { (void)p; memcpy(diskmem + (size_t)b * 4096, buf, 4096); return 0; }
struct blkdrv *virtio_blk_probe(void) { return have_disk ? &drv : NULL; }

static size_t tar_len;
static void tar_add(uint8_t *tar, const char *name, char type, const char *data)
{
    uint8_t *h = tar + tar_len; size_t n = data ? strlen(data) : 0;
    memset(h, 0, 512);
    snprintf((char *)h, 100, "%s", name); snprintf((char *)h + 100, 8, "%07o", type == '5' ? 0755 : 0644);
    snprintf((char *)h + 108, 8, "%07o", 0); snprintf((char *)h + 116, 8, "%07o", 0);
    snprintf((char *)h + 124, 12, "%011lo", (unsigned long)n); snprintf((char *)h + 136, 12, "%011o", 0);
    memset(h + 148, ' ', 8); h[156] = (uint8_t)type; memcpy(h + 257, "ustar", 6); memcpy(h + 263, "00", 2);
    unsigned sum = 0; for (int i = 0; i < 512; i++) sum += h[i];
    snprintf((char *)h + 148, 7, "%06o", sum); h[154] = 0; h[155] = ' ';
    tar_len += 512;
    if (n) { memcpy(tar + tar_len, data, n); tar_len += (n + 511) / 512 * 512; }
}

static int fails;
#define T(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)
static int slurp(const char *p, char *buf, size_t n) { struct vnode *v; if (vfs_lookup(p, vfs_root(), &v) < 0) return -1; long k = vnode_read(v, 0, buf, n - 1); if (k < 0) return -1; buf[k] = 0; return (int)k; }
static int is(const char *p, const char *want) { char b[256]; return slurp(p, b, sizeof b) >= 0 && !strcmp(b, want); }
static void put(const char *p, const char *s) { struct vnode *v; if (vfs_lookup(p, vfs_root(), &v) == 0) vfs_truncate(v, 0); else if (vfs_create(p, vfs_root(), 0644, 0, &v) < 0) { printf("create %s failed\n", p); exit(1); } vnode_write(v, 0, s, strlen(s)); }

static uint8_t tar1[8192], tar2[8192];

int main(void)
{
    diskmem = calloc(NBLK, 4096);
    drv.name = "vda"; drv.nblocks = NBLK; drv.read = mread; drv.write = mwrite;
    have_disk = 1;

    tar_len = 0; tar_add(tar1, "bin/", '5', 0); tar_add(tar1, "bin/sh", '0', "shell v1"); tar_add(tar1, "bin/ls", '0', "ls v1");
    tar_add(tar1, "etc/", '5', 0); tar_add(tar1, "etc/motd", '0', "motd v1\n"); tar_add(tar1, "usr/share/doc/NOTICE", '0', "notice v1");
    size_t len1 = tar_len;
    tar_len = 0; tar_add(tar2, "bin/", '5', 0); tar_add(tar2, "bin/sh", '0', "shell v2 (new build)"); tar_add(tar2, "bin/ls", '0', "ls v1");
    tar_add(tar2, "etc/", '5', 0); tar_add(tar2, "etc/motd", '0', "motd v2\n"); tar_add(tar2, "usr/share/doc/NOTICE", '0', "notice v2");
    size_t len2 = tar_len;

    klog[0] = 0; vfs_init(tar1, len1);
    T(strstr(klog, "lemonfs created"), "blank disk is formatted");
    T(is("/bin/sh", "shell v1") && is("/etc/motd", "motd v1\n"), "initrd unpacked onto the disk");
    vfs_mkdir("/root", vfs_root(), 0700); put("/root/note", "my work");
    put("/etc/motd", "edited by the user\n");
    put("/tmp-file", "scratch");

    klog[0] = 0; vfs_init(tar2, len2);
    T(strstr(klog, "lemonfs mounted") && !strstr(klog, "inconsistent") && !strstr(klog, "consistency"), "existing filesystem is mounted, fsck clean");
    T(is("/root/note", "my work") && is("/tmp-file", "scratch"), "user files survive the reboot");
    T(is("/etc/motd", "edited by the user\n"), "edited configuration is not overwritten");
    T(is("/bin/sh", "shell v2 (new build)"), "programs are refreshed from the new initrd");
    T(is("/usr/share/doc/NOTICE", "notice v2"), "documentation is refreshed too");

    struct vstatfs a, b; vfs_statfs(vfs_root(), &a);
    klog[0] = 0; vfs_init(tar2, len2); vfs_statfs(vfs_root(), &b);
    T(a.bfree == b.bfree && a.ffree == b.ffree, "re-running with the same initrd uses no extra space");

    memset(diskmem, 0, (size_t)NBLK * 4096); memcpy(diskmem + 510, "\x55\xAA", 2); memcpy(diskmem + 4096 * 3, "precious", 8);
    uint8_t *copy = malloc((size_t)NBLK * 4096); memcpy(copy, diskmem, (size_t)NBLK * 4096);
    klog[0] = 0; vfs_init(tar1, len1);
    T(strstr(klog, "not a lemonfs disk"), "foreign disk is recognised");
    T(memcmp(copy, diskmem, (size_t)NBLK * 4096) == 0, "foreign disk is byte-for-byte untouched");
    T(is("/bin/sh", "shell v1"), "system still boots from a RAM disk");

    have_disk = 0; klog[0] = 0; vfs_init(tar1, len1);
    T(strstr(klog, "disk: none"), "no disk: volatile RAM disk");
    T(is("/bin/ls", "ls v1"), "RAM disk populated");

    printf(fails ? "persist: %d FAILURES\n" : "persist: all tests passed\n", fails);
    return fails != 0;
}
