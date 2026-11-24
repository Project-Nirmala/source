#include <net.h>
#include <pci.h>
#include <mm.h>
#include <kern.h>

#define REG_CTRL   0x0000
#define REG_ICR    0x00C0
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_MTA    0x5200
#define REG_RAL    0x5400
#define REG_RAH    0x5404

#define CTRL_SLU   (1u << 6)
#define CTRL_ASDE  (1u << 5)
#define CTRL_RST   (1u << 26)
#define RCTL_EN    (1u << 1)
#define RCTL_BAM   (1u << 15)
#define RCTL_SECRC (1u << 26)
#define TCTL_EN    (1u << 1)
#define TCTL_PSP   (1u << 3)
#define TCTL_CT    (0x10u << 4)
#define TCTL_COLD  (0x40u << 12)

#define RXN        16
#define TXN        8
#define BUFSZ      2048
#define POLL_LIMIT 2000000u

struct rxd {
    uint64_t addr;
    uint16_t len, csum;
    uint8_t  status, err;
    uint16_t special;
};

struct txd {
    uint64_t addr;
    uint16_t len;
    uint8_t  cso, cmd, status, css;
    uint16_t special;
};

static const uint16_t ids[] = { 0x100E, 0x100F, 0x1004, 0x100C, 0x1015, 0x1019, 0x101E, 0x1026, 0x1027, 0x1028 };

static volatile uint8_t *regs;
static volatile struct rxd *rxr;
static volatile struct txd *txr;
static uint8_t *rx_buf[RXN], *tx_buf;
static paddr_t tx_pa;
static unsigned rx_cur, tx_cur;
static bool ready;

static uint32_t rd(uint32_t r)
{
    return *(volatile uint32_t *)(regs + r);
}

static void wr(uint32_t r, uint32_t v)
{
    *(volatile uint32_t *)(regs + r) = v;
}

bool e1000_probe(uint8_t mac[6])
{
    struct pci_dev d;
    paddr_t pa, rxp, txp;
    uint32_t cmd, ral, rah;
    unsigned i;

    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        if (pci_find(0x8086, ids[i], &d))
            break;
    if (i == sizeof(ids) / sizeof(ids[0]) || (d.bar[0] & 1))
        return false;
    cmd = pci_read32(d.bus, d.dev, d.fn, 0x04) & 0xFFFFu;
    pci_write32(d.bus, d.dev, d.fn, 0x04, cmd | 0x2 | 0x4);
    regs = vmm_map_mmio(d.bar[0] & ~0xFu, 0x20000);
    if (!regs)
        return false;

    wr(REG_IMC, 0xFFFFFFFFu);
    wr(REG_CTRL, rd(REG_CTRL) | CTRL_RST);
    for (i = 0; i < POLL_LIMIT && (rd(REG_CTRL) & CTRL_RST); i++)
        __asm__ volatile("pause" : : : "memory");
    wr(REG_IMC, 0xFFFFFFFFu);
    (void)rd(REG_ICR);
    wr(REG_CTRL, (rd(REG_CTRL) | CTRL_SLU | CTRL_ASDE) & ~((1u << 3) | (1u << 30) | (1u << 31)));

    ral = rd(REG_RAL);
    rah = rd(REG_RAH);
    mac[0] = (uint8_t)ral;
    mac[1] = (uint8_t)(ral >> 8);
    mac[2] = (uint8_t)(ral >> 16);
    mac[3] = (uint8_t)(ral >> 24);
    mac[4] = (uint8_t)rah;
    mac[5] = (uint8_t)(rah >> 8);
    wr(REG_RAL, ral);
    wr(REG_RAH, (rah & 0xFFFFu) | (1u << 31));
    for (i = 0; i < 128; i++)
        wr(REG_MTA + 4 * i, 0);

    rxp = pmm_alloc_zero();
    txp = pmm_alloc_zero();
    tx_pa = pmm_alloc_zero();
    if (!rxp || !txp || !tx_pa)
        return false;
    rxr = P2V(rxp);
    txr = P2V(txp);
    tx_buf = P2V(tx_pa);
    for (i = 0; i < RXN; i++) {
        pa = pmm_alloc_zero();
        if (!pa)
            return false;
        rx_buf[i] = P2V(pa);
        rxr[i].addr = pa;
    }
    for (i = 0; i < TXN; i++) {
        txr[i].addr = tx_pa;
        txr[i].status = 1;
    }
    wr(REG_RDBAL, (uint32_t)rxp);
    wr(REG_RDBAH, (uint32_t)(rxp >> 32));
    wr(REG_RDLEN, RXN * sizeof(struct rxd));
    wr(REG_RDH, 0);
    wr(REG_RDT, RXN - 1);
    wr(REG_TDBAL, (uint32_t)txp);
    wr(REG_TDBAH, (uint32_t)(txp >> 32));
    wr(REG_TDLEN, TXN * sizeof(struct txd));
    wr(REG_TDH, 0);
    wr(REG_TDT, 0);
    wr(REG_TIPG, 0x0060200Au);
    wr(REG_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD);
    wr(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);
    rx_cur = tx_cur = 0;
    ready = true;
    return true;
}

int e1000_send(const void *frame, size_t len)
{
    volatile struct txd *t;
    unsigned spins;

    if (!ready || len > BUFSZ)
        return -1;
    t = &txr[tx_cur];
    memcpy(tx_buf, frame, len);
    t->len = (uint16_t)len;
    t->cmd = 0x0B;
    t->status = 0;
    __sync_synchronize();
    tx_cur = (tx_cur + 1) % TXN;
    wr(REG_TDT, tx_cur);
    for (spins = 0; !(t->status & 1); spins++) {
        if (spins > POLL_LIMIT)
            return -1;
        __asm__ volatile("pause" : : : "memory");
    }
    return 0;
}

void e1000_poll(void (*rx)(const uint8_t *, size_t))
{
    if (!ready)
        return;
    (void)rd(REG_ICR);
    while (rxr[rx_cur].status & 1) {
        unsigned len = rxr[rx_cur].len;

        __sync_synchronize();
        if (len && len <= BUFSZ)
            rx(rx_buf[rx_cur], len);
        rxr[rx_cur].status = 0;
        __sync_synchronize();
        wr(REG_RDT, rx_cur);
        rx_cur = (rx_cur + 1) % RXN;
    }
}
