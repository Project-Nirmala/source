#include <blk.h>
#include <sys/errno.h>

#ifdef VIRTIO_HOST_TEST
#include "virtio_host.h"
#else
#include <io.h>
#include <pci.h>
#include <mm.h>
#include <kern.h>
#endif

#define VIRTIO_VENDOR    0x1AF4
#define VIRTIO_BLK_LEGACY 0x1001
#define VIRTIO_BLK_MODERN 0x1042

#define REG_HOST_FEATURES  0
#define REG_GUEST_FEATURES 4
#define REG_QUEUE_PFN      8
#define REG_QUEUE_NUM      12
#define REG_QUEUE_SEL      14
#define REG_QUEUE_NOTIFY   16
#define REG_STATUS         18
#define REG_ISR            19
#define REG_CONFIG         20

#define ST_ACK        1
#define ST_DRIVER     2
#define ST_DRIVER_OK  4
#define ST_FAILED     128

#define VRING_NEXT  1
#define VRING_WRITE 2

#define BLK_T_IN  0
#define BLK_T_OUT 1

#define SECTORS_PER_BLOCK (LEMON_BSIZE / 512)
#define POLL_LIMIT        20000000u

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
};

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
};

struct blk_req_hdr {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
};

struct vblk {
    uint16_t            io;
    uint16_t            qsz;
    struct vring_desc  *desc;
    volatile struct vring_avail *avail;
    volatile struct vring_used  *used;
    uint16_t            avail_idx, last_used;
    paddr_t             req_pa, data_pa;
    uint8_t            *req, *data;
    bool                dead;
};

static struct vblk vb;
static struct blkdrv drv;

static int vblk_io(void *priv, int write, uint32_t blk, void *buf)
{
    struct vblk *v = priv;
    struct blk_req_hdr *h = (struct blk_req_hdr *)v->req;
    volatile uint8_t *status = v->req + sizeof(*h);
    unsigned spins;

    if (v->dead)
        return -EIO;
    h->type = write ? BLK_T_OUT : BLK_T_IN;
    h->ioprio = 0;
    h->sector = (uint64_t)blk * SECTORS_PER_BLOCK;
    *status = 0xFF;
    if (write)
        memcpy(v->data, buf, LEMON_BSIZE);

    v->desc[0].addr = v->req_pa;
    v->desc[0].len = sizeof(*h);
    v->desc[0].flags = VRING_NEXT;
    v->desc[0].next = 1;
    v->desc[1].addr = v->data_pa;
    v->desc[1].len = LEMON_BSIZE;
    v->desc[1].flags = (uint16_t)(VRING_NEXT | (write ? 0 : VRING_WRITE));
    v->desc[1].next = 2;
    v->desc[2].addr = v->req_pa + sizeof(*h);
    v->desc[2].len = 1;
    v->desc[2].flags = VRING_WRITE;
    v->desc[2].next = 0;

    v->avail->ring[v->avail_idx % v->qsz] = 0;
    __sync_synchronize();
    v->avail_idx++;
    v->avail->idx = v->avail_idx;
    __sync_synchronize();
    outw((uint16_t)(v->io + REG_QUEUE_NOTIFY), 0);

    for (spins = 0; v->used->idx == v->last_used; spins++) {
        if (spins > POLL_LIMIT) {
            v->dead = true;
            kprintf("virtio-blk: request timed out, disk disabled\n");
            return -EIO;
        }
        __asm__ volatile("pause" : : : "memory");
    }
    __sync_synchronize();
    v->last_used++;
    (void)inb((uint16_t)(v->io + REG_ISR));
    if (*status != 0)
        return -EIO;
    if (!write)
        memcpy(buf, v->data, LEMON_BSIZE);
    return 0;
}

static int vblk_read(void *priv, uint32_t blk, void *buf)
{
    return vblk_io(priv, 0, blk, buf);
}

static int vblk_write(void *priv, uint32_t blk, const void *buf)
{
    return vblk_io(priv, 1, blk, (void *)buf);
}

struct blkdrv *virtio_blk_attach(uint16_t io)
{
    struct vblk *v = &vb;
    uint32_t desc_bytes, avail_bytes, used_off, total, pages;
    uint64_t sectors;
    paddr_t qpa;
    uint8_t *q;

    outb((uint16_t)(io + REG_STATUS), 0);
    outb((uint16_t)(io + REG_STATUS), ST_ACK);
    outb((uint16_t)(io + REG_STATUS), ST_ACK | ST_DRIVER);
    (void)inl((uint16_t)(io + REG_HOST_FEATURES));
    outl((uint16_t)(io + REG_GUEST_FEATURES), 0);

    outw((uint16_t)(io + REG_QUEUE_SEL), 0);
    v->qsz = inw((uint16_t)(io + REG_QUEUE_NUM));
    if (v->qsz == 0 || v->qsz > 32768 || (v->qsz & (v->qsz - 1))) {
        outb((uint16_t)(io + REG_STATUS), ST_FAILED);
        return NULL;
    }
    desc_bytes = 16u * v->qsz;
    avail_bytes = 6u + 2u * v->qsz;
    used_off = (desc_bytes + avail_bytes + 4095u) & ~4095u;
    total = used_off + 6u + 8u * v->qsz;
    pages = (total + 4095u) / 4096u;
    qpa = pmm_alloc_contig(pages);
    v->req_pa = pmm_alloc_zero();
    v->data_pa = pmm_alloc_zero();
    if (!qpa || !v->req_pa || !v->data_pa) {
        outb((uint16_t)(io + REG_STATUS), ST_FAILED);
        return NULL;
    }
    q = P2V(qpa);
    v->io = io;
    v->desc = (struct vring_desc *)q;
    v->avail = (volatile struct vring_avail *)(q + desc_bytes);
    v->used = (volatile struct vring_used *)(q + used_off);
    v->req = P2V(v->req_pa);
    v->data = P2V(v->data_pa);
    v->avail_idx = v->last_used = 0;
    v->dead = false;
    outl((uint16_t)(io + REG_QUEUE_PFN), (uint32_t)(qpa >> 12));
    outb((uint16_t)(io + REG_STATUS), ST_ACK | ST_DRIVER | ST_DRIVER_OK);

    sectors = inl((uint16_t)(io + REG_CONFIG)) | (uint64_t)inl((uint16_t)(io + REG_CONFIG + 4)) << 32;
    drv.name = "vda";
    drv.nblocks = (uint32_t)(sectors / SECTORS_PER_BLOCK > 0xFFFFFFFFu ? 0xFFFFFFFFu
                                                                      : sectors / SECTORS_PER_BLOCK);
    drv.priv = v;
    drv.read = vblk_read;
    drv.write = vblk_write;
    return drv.nblocks ? &drv : NULL;
}

#ifndef VIRTIO_HOST_TEST
struct blkdrv *virtio_blk_probe(void)
{
    struct pci_dev d;

    if (pci_find(VIRTIO_VENDOR, VIRTIO_BLK_LEGACY, &d)) {
        if (!(d.bar[0] & 1)) {
            kprintf("virtio-blk: BAR0 is not an I/O port range\n");
            return NULL;
        }
        pci_enable_io_master(&d);
        return virtio_blk_attach((uint16_t)(d.bar[0] & ~3u));
    }
    if (pci_find(VIRTIO_VENDOR, VIRTIO_BLK_MODERN, &d))
        kprintf("virtio-blk: found a modern-only device; start QEMU with disable-modern=on\n");
    return NULL;
}
#endif
