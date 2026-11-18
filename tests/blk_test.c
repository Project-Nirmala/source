#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <blk.h>
#include <sys/errno.h>
#include "virtio_host.h"

uint64_t hhdm_offset;
#define IO 0xC040

void *kmalloc(size_t n) { return malloc(n); }
void *kzalloc(size_t n) { return calloc(1, n); }
void kfree(void *p) { free(p); }
void kprintf(const char *f, ...) { va_list ap; va_start(ap, f); printf("  [kernel] "); vprintf(f, ap); va_end(ap); }
void panic(const char *f, ...) { va_list ap; va_start(ap, f); printf("PANIC: "); vprintf(f, ap); printf("\n"); va_end(ap); abort(); }

static uint8_t *arena; static size_t arena_used;
static void arena_init(void) { arena = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0); if (arena == MAP_FAILED) { perror("mmap"); exit(2); } }
paddr_t pmm_alloc_contig(unsigned pages) { paddr_t p = (paddr_t)(uintptr_t)(arena + arena_used); arena_used += (size_t)pages * 4096; return p; }
paddr_t pmm_alloc_zero(void) { return pmm_alloc_contig(1); }

#define DISK_BLOCKS 1024
static uint8_t *disk;
static uint8_t  dev_status, status_log[16]; static int nstatus;
static uint32_t guest_features = 0xDEAD, queue_pfn; static uint16_t qsel, qnum = 256, last_avail;
static int fail_next, drop_notify, requests, reads, writes;
struct vd { uint64_t addr; uint32_t len; uint16_t flags, next; };

static void process(void)
{
    uint8_t *q = (uint8_t *)(uintptr_t)((uint64_t)queue_pfn << 12);
    struct vd *desc = (struct vd *)q;
    uint16_t *avail = (uint16_t *)(q + 16u * qnum);
    uint32_t used_off = (16u * qnum + 6u + 2u * qnum + 4095u) & ~4095u;
    uint16_t *used = (uint16_t *)(q + used_off);
    while (last_avail != avail[1]) {
        uint16_t head = avail[2 + last_avail % qnum];
        struct vd *d0 = &desc[head], *d1 = &desc[d0->next], *d2 = &desc[d1->next];
        uint32_t type = ((uint32_t *)(uintptr_t)d0->addr)[0]; uint64_t sector = ((uint64_t *)(uintptr_t)d0->addr)[1];
        uint8_t *status = (uint8_t *)(uintptr_t)d2->addr;
        int ok = (d0->flags == 1 && d0->len == 16 && d1->flags == (type == 0 ? 3 : 1) && d1->len == 4096 &&
                  d2->flags == 2 && d2->len == 1 && sector % 8 == 0 && sector / 8 < DISK_BLOCKS);
        if (!ok) { printf("FAIL: malformed request chain (type %u sector %llu f0=%u f1=%u f2=%u)\n", type, (unsigned long long)sector, d0->flags, d1->flags, d2->flags); exit(1); }
        requests++;
        if (fail_next) { *status = 1; fail_next = 0; }
        else {
            if (type == 0) { memcpy((void *)(uintptr_t)d1->addr, disk + sector * 512, 4096); reads++; }
            else { memcpy(disk + sector * 512, (void *)(uintptr_t)d1->addr, 4096); writes++; }
            *status = 0;
        }
        struct { uint32_t id, len; } *ring = (void *)(used + 2);
        ring[used[1] % qnum].id = head; ring[used[1] % qnum].len = 4097;
        __sync_synchronize(); used[1]++; last_avail++;
    }
}
void outb(uint16_t p, uint8_t v) { if (p == IO + 18) { dev_status = v; if (nstatus < 16) status_log[nstatus++] = v; if (v == 0) { last_avail = 0; } } }
void outw(uint16_t p, uint16_t v) { if (p == IO + 14) qsel = v; else if (p == IO + 16 && !drop_notify) process(); }
void outl(uint16_t p, uint32_t v) { if (p == IO + 4) guest_features = v; else if (p == IO + 8) queue_pfn = v; }
uint8_t inb(uint16_t p) { return p == IO + 19 ? 1 : p == IO + 18 ? dev_status : 0; }
uint16_t inw(uint16_t p) { return p == IO + 12 && qsel == 0 ? qnum : 0; }
uint32_t inl(uint16_t p) { if (p == IO) return 0xFFFFFFFF; if (p == IO + 20) return (uint32_t)(DISK_BLOCKS * 8); if (p == IO + 24) return 0; return 0; }

static int fails;
#define T(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)

int main(void)
{
    arena_init();
    disk = calloc(DISK_BLOCKS, 4096);
    struct blkdrv *drv = virtio_blk_attach(IO);
    T(drv != NULL && drv->nblocks == DISK_BLOCKS, "attach finds the disk and its size");
    T(nstatus >= 4 && status_log[0] == 0 && status_log[1] == 1 && status_log[2] == 3 && status_log[nstatus - 1] == 7, "status handshake: reset, ACK, DRIVER, DRIVER_OK");
    T(guest_features == 0 && queue_pfn != 0, "no features negotiated, queue address set");

    uint8_t a[4096], b[4096];
    for (int i = 0; i < 4096; i++) a[i] = (uint8_t)(i * 7 + 1);
    T(drv->write(drv->priv, 5, a) == 0, "write block");
    T(memcmp(disk + 5 * 4096, a, 4096) == 0, "data reached the device at the right sector");
    T(drv->read(drv->priv, 5, b) == 0 && !memcmp(a, b, 4096), "read it back");
    for (int i = 0; i < 600; i++) { a[0] = (uint8_t)i; if (drv->write(drv->priv, (uint32_t)(i % 100) + 10, a)) { T(0, "ring wrap write"); break; } }
    T(requests > 600, "many requests across ring wrap-around (idx passes 256)");
    fail_next = 1; T(drv->write(drv->priv, 7, a) == -EIO, "device error status gives -EIO");
    T(drv->write(drv->priv, 7, a) == 0, "driver recovers after an error");
    drop_notify = 1; T(drv->read(drv->priv, 5, b) == -EIO, "no completion: polling gives up with -EIO"); drop_notify = 0;
    process();
    T(drv->read(drv->priv, 5, b) == -EIO, "...but the driver stays disabled rather than run with unknown ring state");

    memset(disk, 0, (size_t)DISK_BLOCKS * 4096);
    drv = virtio_blk_attach(IO);
    struct blkdev *dev = blkcache_create(drv, 8);
    T(lemon_mkfs(dev, 256, "blk") == 0, "mkfs through cache");
    struct lemon L; T(lemon_mount(&L, dev) == 0, "mount");
    uint32_t f1, f2, d1;
    T(lemon_ialloc(&L, LEMON_IFREG | 0644, &f1) == 0 && lemon_dir_add(&L, 1, "big", f1, LEMON_DT_REG) == 0, "create file");
    static uint8_t pat[300000], got[300000];
    for (size_t i = 0; i < sizeof pat; i++) pat[i] = (uint8_t)(i * 13 + (i >> 8));
    T(lemon_write(&L, f1, 0, pat, sizeof pat) == (long)sizeof pat, "write 300 KB (more than the 8-buffer cache holds)");
    T(lemon_ialloc(&L, LEMON_IFDIR | 0755, &d1) == 0 && lemon_dir_add(&L, 1, "sub", d1, LEMON_DT_DIR) == 0, "mkdir");
    T(lemon_ialloc(&L, LEMON_IFREG | 0644, &f2) == 0 && lemon_dir_add(&L, d1, "small", f2, LEMON_DT_REG) == 0, "create nested file");
    T(lemon_write(&L, f2, 0, "persistent!", 11) == 11, "write small");
    T(lemon_truncate(&L, f1, 250000) == 0, "truncate");
    printf("  device saw %d reads and %d writes while building the filesystem\n", reads, writes);

    drv = virtio_blk_attach(IO);
    dev = blkcache_create(drv, 8);
    struct lemon L2; T(lemon_mount(&L2, dev) == 0, "remount after reboot");
    uint8_t *scratch = malloc(dev->nblocks); T(lemon_check(&L2, scratch) == 0, "fsck clean after reboot"); free(scratch);
    uint32_t i1, i2; int ty;
    T(lemon_dir_lookup(&L2, 1, "big", &i1, &ty) == 0 && lemon_read(&L2, i1, 0, got, sizeof got) == 250000 && !memcmp(got, pat, 250000), "big file intact");
    T(lemon_dir_lookup(&L2, 1, "sub", &i2, &ty) == 0 && lemon_dir_lookup(&L2, i2, "small", &i2, &ty) == 0 && lemon_read(&L2, i2, 0, got, 64) == 11 && !memcmp(got, "persistent!", 11), "nested file intact");
    printf(fails ? "blk: %d FAILURES\n" : "blk: all tests passed\n", fails);
    return fails != 0;
}
