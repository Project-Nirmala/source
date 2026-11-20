#include <blk.h>
#include <pci.h>
#include <mm.h>
#include <kern.h>
#include <sys/errno.h>

#define PCI_CLASS_AHCI 0x010601u

#define HBA_CAP 0x00
#define HBA_GHC 0x04
#define HBA_PI  0x0C

#define GHC_AE   (1u << 31)
#define CAP_S64A (1u << 31)

#define PORT_BASE(n) (0x100u + (n) * 0x80u)

#define P_CLB  0x00
#define P_CLBU 0x04
#define P_FB   0x08
#define P_FBU  0x0C
#define P_IS   0x10
#define P_CMD  0x18
#define P_TFD  0x20
#define P_SIG  0x24
#define P_SSTS 0x28
#define P_SERR 0x30
#define P_CI   0x38

#define CMD_ST  (1u << 0)
#define CMD_FRE (1u << 4)
#define CMD_FR  (1u << 14)
#define CMD_CR  (1u << 15)

#define TFD_ERR 0x01u
#define TFD_DRQ 0x08u
#define TFD_BSY 0x80u

#define IS_TFES (1u << 30)

#define SIG_ATA 0x00000101u

#define ATA_IDENTIFY 0xEC
#define ATA_READ48   0x25
#define ATA_WRITE48  0x35
#define ATA_FLUSH48  0xEA

#define AHCI_MAX_DISKS    8
#define DATA_PAGES        16
#define MAX_SECTORS       (DATA_PAGES * 4096 / 512)

#define SECTORS_PER_BLOCK (LEMON_BSIZE / 512)
#define POLL_LIMIT        20000000u

struct ahci_hdr {
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsvd[4];
};

struct ahci_prd {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsvd;
    uint32_t dbc;
};

struct ahci_tbl {
    uint8_t         cfis[64];
    uint8_t         acmd[16];
    uint8_t         rsvd[48];
    struct ahci_prd prd[1];
};

struct ahci {
    volatile uint8_t *port;
    struct ahci_hdr  *hdr;
    struct ahci_tbl  *tbl;
    uint8_t          *data;
    paddr_t           tbl_pa, data_pa;
    bool              dead;
    char              name[4];
    struct blkdrv     drv;
};

static struct ahci disks[AHCI_MAX_DISKS];
static unsigned    ndisks;

static uint32_t rd(volatile uint8_t *base, uint32_t off)
{
    return *(volatile uint32_t *)(base + off);
}

static void wr(volatile uint8_t *base, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(base + off) = v;
}

static bool wait_clear(volatile uint8_t *port, uint32_t off, uint32_t mask)
{
    unsigned spins;

    for (spins = 0; rd(port, off) & mask; spins++) {
        if (spins > POLL_LIMIT)
            return false;
        __asm__ volatile("pause" : : : "memory");
    }
    return true;
}

static bool port_stop(volatile uint8_t *port)
{
    wr(port, P_CMD, rd(port, P_CMD) & ~(CMD_ST | CMD_FRE));
    return wait_clear(port, P_CMD, CMD_CR | CMD_FR);
}

static void port_start(volatile uint8_t *port)
{
    wr(port, P_SERR, 0xFFFFFFFFu);
    wr(port, P_IS, 0xFFFFFFFFu);
    wr(port, P_CMD, rd(port, P_CMD) | CMD_FRE);
    wr(port, P_CMD, rd(port, P_CMD) | CMD_ST);
}

static void port_recover(struct ahci *a)
{
    if (!port_stop(a->port)) {
        a->dead = true;
        return;
    }
    port_start(a->port);
}

static int ahci_cmd(struct ahci *a, uint8_t cmd, uint64_t lba, unsigned count, bool write, unsigned bytes)
{
    volatile uint8_t *port = a->port;
    uint8_t *f = a->tbl->cfis;
    unsigned spins, limit = bytes ? POLL_LIMIT : POLL_LIMIT * 8;
    uint32_t tfd;
    unsigned i;

    if (a->dead)
        return -EIO;
    for (i = 0; i < sizeof(a->tbl->cfis); i++)
        f[i] = 0;
    f[0] = 0x27;
    f[1] = 0x80;
    f[2] = cmd;
    if (cmd != ATA_IDENTIFY) {
        f[4] = (uint8_t)lba;
        f[5] = (uint8_t)(lba >> 8);
        f[6] = (uint8_t)(lba >> 16);
        f[7] = 0x40;
        f[8] = (uint8_t)(lba >> 24);
        f[9] = (uint8_t)(lba >> 32);
        f[10] = (uint8_t)(lba >> 40);
        f[12] = (uint8_t)count;
        f[13] = (uint8_t)(count >> 8);
    }
    if (bytes) {
        a->tbl->prd[0].dba = (uint32_t)a->data_pa;
        a->tbl->prd[0].dbau = (uint32_t)(a->data_pa >> 32);
        a->tbl->prd[0].rsvd = 0;
        a->tbl->prd[0].dbc = bytes - 1;
    }
    a->hdr[0].flags = (uint16_t)(5 | (write ? 1 << 6 : 0));
    a->hdr[0].prdtl = bytes ? 1 : 0;
    a->hdr[0].prdbc = 0;

    if (!wait_clear(port, P_TFD, TFD_BSY | TFD_DRQ)) {
        port_recover(a);
        if (a->dead || !wait_clear(port, P_TFD, TFD_BSY | TFD_DRQ)) {
            a->dead = true;
            kprintf("%s: device busy, disk disabled\n", a->name);
            return -EIO;
        }
    }
    wr(port, P_IS, 0xFFFFFFFFu);
    __sync_synchronize();
    wr(port, P_CI, 1);
    for (spins = 0; rd(port, P_CI) & 1; spins++) {
        if (rd(port, P_IS) & IS_TFES)
            break;
        if (spins > limit) {
            a->dead = true;
            kprintf("%s: request timed out, disk disabled\n", a->name);
            return -EIO;
        }
        __asm__ volatile("pause" : : : "memory");
    }
    __sync_synchronize();
    tfd = rd(port, P_TFD);
    if ((rd(port, P_IS) & IS_TFES) || (tfd & (TFD_ERR | TFD_BSY | TFD_DRQ))) {
        kprintf("%s: command %x failed at sector %lu\n", a->name, cmd, lba);
        port_recover(a);
        return -EIO;
    }
    return 0;
}

static int ahci_rw(void *priv, uint64_t lba, uint32_t count, void *buf, int write)
{
    struct ahci *a = priv;
    uint8_t *p = buf;

    while (count) {
        uint32_t n = count > MAX_SECTORS ? MAX_SECTORS : count;

        if (write)
            memcpy(a->data, p, (size_t)n * 512);
        if (ahci_cmd(a, write ? ATA_WRITE48 : ATA_READ48, lba, n, write != 0, n * 512) < 0)
            return -EIO;
        if (!write)
            memcpy(p, a->data, (size_t)n * 512);
        lba += n;
        count -= n;
        p += (size_t)n * 512;
    }
    return 0;
}

static int ahci_flush(void *priv)
{
    return ahci_cmd(priv, ATA_FLUSH48, 0, 0, false, 0);
}

static int ahci_read(void *priv, uint32_t blk, void *buf)
{
    return ahci_rw(priv, (uint64_t)blk * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK, buf, 0);
}

static int ahci_write(void *priv, uint32_t blk, const void *buf)
{
    return ahci_rw(priv, (uint64_t)blk * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK, (void *)buf, 1);
}

static bool port_ready(volatile uint8_t *port)
{
    uint32_t ssts = rd(port, P_SSTS);

    return (ssts & 0xF) == 3 && ((ssts >> 8) & 0xF) == 1 && rd(port, P_SIG) == SIG_ATA;
}

static struct ahci *ahci_attach(volatile uint8_t *regs, unsigned n, bool s64a)
{
    struct ahci *a = &disks[ndisks];
    volatile uint8_t *port = regs + PORT_BASE(n);
    paddr_t clb_pa, fb_pa;
    uint16_t *id;
    uint64_t sectors;
    unsigned i, len;

    if (!port_stop(port)) {
        kprintf("ahci: port %u would not stop\n", n);
        return NULL;
    }
    clb_pa = pmm_alloc_zero();
    fb_pa = pmm_alloc_zero();
    a->tbl_pa = pmm_alloc_zero();
    a->data_pa = pmm_alloc_contig(DATA_PAGES);
    if (!clb_pa || !fb_pa || !a->tbl_pa || !a->data_pa)
        return NULL;
    if (!s64a && ((clb_pa | fb_pa | a->tbl_pa | (a->data_pa + DATA_PAGES * 4096)) >> 32)) {
        kprintf("ahci: controller cannot reach memory above 4 GiB\n");
        return NULL;
    }
    a->port = port;
    a->hdr = P2V(clb_pa);
    a->tbl = P2V(a->tbl_pa);
    a->data = P2V(a->data_pa);
    a->dead = false;
    a->hdr[0].ctba = (uint32_t)a->tbl_pa;
    a->hdr[0].ctbau = (uint32_t)(a->tbl_pa >> 32);

    wr(port, P_CLB, (uint32_t)clb_pa);
    wr(port, P_CLBU, (uint32_t)(clb_pa >> 32));
    wr(port, P_FB, (uint32_t)fb_pa);
    wr(port, P_FBU, (uint32_t)(fb_pa >> 32));
    port_start(port);

    if (ahci_cmd(a, ATA_IDENTIFY, 0, 0, false, 512) < 0) {
        kprintf("ahci: IDENTIFY failed on port %u\n", n);
        return NULL;
    }
    id = (uint16_t *)a->data;
    if (!(id[83] & (1u << 10))) {
        kprintf("ahci: disk has no 48-bit LBA support\n");
        return NULL;
    }
    if (id[106] & (1u << 12)) {
        kprintf("ahci: logical sectors larger than 512 bytes are not supported\n");
        return NULL;
    }
    sectors = (uint64_t)id[100] | (uint64_t)id[101] << 16 | (uint64_t)id[102] << 32 | (uint64_t)id[103] << 48;
    for (i = 0; i < 20; i++) {
        a->drv.model[2 * i] = (char)(id[27 + i] >> 8);
        a->drv.model[2 * i + 1] = (char)(id[27 + i] & 0xFF);
    }
    for (len = 40; len > 0 && (a->drv.model[len - 1] == ' ' || a->drv.model[len - 1] == 0); len--)
        ;
    a->drv.model[len] = 0;
    a->name[0] = 's';
    a->name[1] = 'd';
    a->name[2] = (char)('a' + ndisks);
    a->name[3] = 0;
    a->drv.name = a->name;
    a->drv.nsectors = sectors;
    a->drv.nblocks = (uint32_t)(sectors / SECTORS_PER_BLOCK > 0xFFFFFFFFu ? 0xFFFFFFFFu
                                                                         : sectors / SECTORS_PER_BLOCK);
    a->drv.priv = a;
    a->drv.read = ahci_read;
    a->drv.write = ahci_write;
    a->drv.rw = ahci_rw;
    a->drv.flush = ahci_flush;
    return sectors ? a : NULL;
}

struct blkdrv *ahci_probe(void)
{
    struct pci_dev d;
    volatile uint8_t *regs;
    uint32_t cmd, pi, cap;
    unsigned n;
    struct blkdrv *first = NULL;

    if (!pci_find_class(PCI_CLASS_AHCI, &d))
        return NULL;
    if (d.bar[5] & 1) {
        kprintf("ahci: BAR5 is not a memory range\n");
        return NULL;
    }
    cmd = pci_read32(d.bus, d.dev, d.fn, 0x04);
    pci_write32(d.bus, d.dev, d.fn, 0x04, cmd | 0x2 | 0x4 | 0x400);
    regs = vmm_map_mmio(d.bar[5] & ~0xFu, 0x2000);
    wr(regs, HBA_GHC, rd(regs, HBA_GHC) | GHC_AE);
    cap = rd(regs, HBA_CAP);
    pi = rd(regs, HBA_PI);
    for (n = 0; n < 32 && ndisks < AHCI_MAX_DISKS; n++) {
        struct ahci *a;

        if (!(pi & (1u << n)) || !port_ready(regs + PORT_BASE(n)))
            continue;
        a = ahci_attach(regs, n, (cap & CAP_S64A) != 0);
        if (!a)
            continue;
        ndisks++;
        blk_register(&a->drv);
        kprintf("[ok] %s: %s, %lu MiB\n", a->name, a->drv.model, a->drv.nsectors / 2048);
        if (!first)
            first = &a->drv;
    }
    return first;
}
