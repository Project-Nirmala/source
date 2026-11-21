#include <net.h>
#include <io.h>
#include <pci.h>
#include <mm.h>
#include <kern.h>

#define VIRTIO_VENDOR     0x1AF4
#define VIRTIO_NET_LEGACY 0x1000

#define REG_HOST_FEATURES  0
#define REG_GUEST_FEATURES 4
#define REG_QUEUE_PFN      8
#define REG_QUEUE_NUM      12
#define REG_QUEUE_SEL      14
#define REG_QUEUE_NOTIFY   16
#define REG_STATUS         18
#define REG_ISR            19
#define REG_CONFIG         20

#define ST_ACK       1
#define ST_DRIVER    2
#define ST_DRIVER_OK 4
#define ST_FAILED    128

#define F_MAC       (1u << 5)
#define VRING_WRITE 2
#define HDR_LEN     10
#define RXN         16
#define BUFSZ       4096
#define POLL_LIMIT  2000000u

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags, next;
};

struct vq {
    uint16_t qsz, avail_idx, last_used;
    struct vring_desc *desc;
    volatile uint16_t *avail;
    volatile uint16_t *used;
};

static uint16_t io;
static struct vq rxq, txq;
static paddr_t rx_pa[RXN], tx_pa;
static uint8_t *rx_buf[RXN], *tx_buf;
static bool ready;

static bool vq_setup(struct vq *q, uint16_t idx)
{
    uint32_t desc_bytes, avail_bytes, used_off, total;
    paddr_t pa;
    uint8_t *m;

    outw((uint16_t)(io + REG_QUEUE_SEL), idx);
    q->qsz = inw((uint16_t)(io + REG_QUEUE_NUM));
    if (q->qsz == 0 || q->qsz > 32768 || (q->qsz & (q->qsz - 1)))
        return false;
    desc_bytes = 16u * q->qsz;
    avail_bytes = 6u + 2u * q->qsz;
    used_off = (desc_bytes + avail_bytes + 4095u) & ~4095u;
    total = used_off + 6u + 8u * q->qsz;
    pa = pmm_alloc_contig((total + 4095u) / 4096u);
    if (!pa)
        return false;
    m = P2V(pa);
    memset(m, 0, total);
    q->desc = (struct vring_desc *)m;
    q->avail = (volatile uint16_t *)(m + desc_bytes);
    q->used = (volatile uint16_t *)(m + used_off);
    q->avail_idx = q->last_used = 0;
    outl((uint16_t)(io + REG_QUEUE_PFN), (uint32_t)(pa >> 12));
    return true;
}

static void rx_post(unsigned i)
{
    rxq.avail[2 + rxq.avail_idx % rxq.qsz] = (uint16_t)i;
    __sync_synchronize();
    rxq.avail_idx++;
    rxq.avail[1] = rxq.avail_idx;
    __sync_synchronize();
    outw((uint16_t)(io + REG_QUEUE_NOTIFY), 0);
}

bool vnet_probe(uint8_t mac[6])
{
    struct pci_dev d;
    uint32_t host;
    unsigned i;

    if (!pci_find(VIRTIO_VENDOR, VIRTIO_NET_LEGACY, &d) || !(d.bar[0] & 1))
        return false;
    pci_enable_io_master(&d);
    io = (uint16_t)(d.bar[0] & ~3u);
    outb((uint16_t)(io + REG_STATUS), 0);
    outb((uint16_t)(io + REG_STATUS), ST_ACK);
    outb((uint16_t)(io + REG_STATUS), ST_ACK | ST_DRIVER);
    host = inl((uint16_t)(io + REG_HOST_FEATURES));
    if (!(host & F_MAC))
        goto fail;
    outl((uint16_t)(io + REG_GUEST_FEATURES), F_MAC);
    if (!vq_setup(&rxq, 0) || !vq_setup(&txq, 1))
        goto fail;
    for (i = 0; i < RXN; i++) {
        rx_pa[i] = pmm_alloc_zero();
        if (!rx_pa[i])
            goto fail;
        rx_buf[i] = P2V(rx_pa[i]);
        rxq.desc[i].addr = rx_pa[i];
        rxq.desc[i].len = BUFSZ;
        rxq.desc[i].flags = VRING_WRITE;
    }
    tx_pa = pmm_alloc_zero();
    if (!tx_pa)
        goto fail;
    tx_buf = P2V(tx_pa);
    txq.desc[0].addr = tx_pa;
    for (i = 0; i < 6; i++)
        mac[i] = inb((uint16_t)(io + REG_CONFIG + i));
    outb((uint16_t)(io + REG_STATUS), ST_ACK | ST_DRIVER | ST_DRIVER_OK);
    for (i = 0; i < RXN; i++)
        rx_post(i);
    ready = true;
    return true;
fail:
    outb((uint16_t)(io + REG_STATUS), ST_FAILED);
    return false;
}

int vnet_send(const void *frame, size_t len)
{
    unsigned spins;

    if (!ready || len > BUFSZ - HDR_LEN)
        return -1;
    memset(tx_buf, 0, HDR_LEN);
    memcpy(tx_buf + HDR_LEN, frame, len);
    txq.desc[0].len = (uint32_t)(len + HDR_LEN);
    txq.desc[0].flags = 0;
    txq.avail[2 + txq.avail_idx % txq.qsz] = 0;
    __sync_synchronize();
    txq.avail_idx++;
    txq.avail[1] = txq.avail_idx;
    __sync_synchronize();
    outw((uint16_t)(io + REG_QUEUE_NOTIFY), 1);
    for (spins = 0; txq.used[1] == txq.last_used; spins++) {
        if (spins > POLL_LIMIT)
            return -1;
        __asm__ volatile("pause" : : : "memory");
    }
    __sync_synchronize();
    txq.last_used++;
    return 0;
}

void vnet_poll(void (*rx)(const uint8_t *, size_t))
{
    if (!ready)
        return;
    (void)inb((uint16_t)(io + REG_ISR));
    while (rxq.used[1] != rxq.last_used) {
        uint8_t *e = (uint8_t *)rxq.used + 4 + 8u * (rxq.last_used % rxq.qsz);
        uint32_t id = *(volatile uint32_t *)e, len = *(volatile uint32_t *)(e + 4);

        __sync_synchronize();
        rxq.last_used++;
        if (id < RXN) {
            if (len > HDR_LEN && len <= BUFSZ)
                rx(rx_buf[id] + HDR_LEN, len - HDR_LEN);
            rx_post(id);
        }
    }
}
