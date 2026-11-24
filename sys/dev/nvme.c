#include <blk.h>
#include <pci.h>
#include <mm.h>
#include <kern.h>
#include <sys/errno.h>

#define PCI_CLASS_NVME 0x010802u
#define SECTORS_PER_BLOCK (LEMON_BSIZE / 512)
#define SPIN_LIMIT 100000000u
#define QSZ        64u
#define CHUNK      16u

#define R_CAP  0x00
#define R_CC   0x14
#define R_CSTS 0x1C
#define R_AQA  0x24
#define R_ASQ  0x28
#define R_ACQ  0x30

struct q {
    uint32_t *sq, *cq;
    volatile uint32_t *sqdb, *cqdb;
    uint16_t tail, head, qsz;
    uint8_t  phase;
};

static volatile uint8_t *regs;
static struct q adm, ioq;
static uint16_t cid;
static uint8_t *bounce;
static paddr_t bounce_pa;
static struct blkdrv drv;
static char name[] = "nvme0n1";

static uint32_t rd(uint32_t r)
{
    return *(volatile uint32_t *)(regs + r);
}

static void wr(uint32_t r, uint32_t v)
{
    *(volatile uint32_t *)(regs + r) = v;
}

static int submit(struct q *q, uint32_t c[16])
{
    volatile uint32_t *e;
    unsigned i, spins;
    uint32_t st;

    c[0] = (c[0] & 0xFFFFu) | ((uint32_t)++cid << 16);
    for (i = 0; i < 16; i++)
        q->sq[16 * q->tail + i] = c[i];
    __sync_synchronize();
    q->tail = (uint16_t)((q->tail + 1) % q->qsz);
    *q->sqdb = q->tail;
    e = q->cq + 4 * q->head;
    for (spins = 0; ((e[3] >> 16) & 1) != q->phase; spins++) {
        if (spins > SPIN_LIMIT)
            return -EIO;
        __asm__ volatile("pause" : : : "memory");
    }
    __sync_synchronize();
    st = (e[3] >> 17) & 0x7FFF;
    if (++q->head == q->qsz) {
        q->head = 0;
        q->phase ^= 1;
    }
    *q->cqdb = q->head;
    return st ? -EIO : 0;
}

static int nvme_rw(void *priv, uint64_t lba, uint32_t count, void *buf, int write)
{
    uint8_t *p = buf;

    (void)priv;
    while (count) {
        uint32_t n = count > CHUNK ? CHUNK : count, c[16];
        size_t bytes = (size_t)n * 512;

        memset(c, 0, sizeof(c));
        c[0] = write ? 1 : 2;
        c[1] = 1;
        c[6] = (uint32_t)bounce_pa;
        c[7] = (uint32_t)(bounce_pa >> 32);
        if (bytes > 4096) {
            c[8] = (uint32_t)(bounce_pa + 4096);
            c[9] = (uint32_t)((bounce_pa + 4096) >> 32);
        }
        c[10] = (uint32_t)lba;
        c[11] = (uint32_t)(lba >> 32);
        c[12] = n - 1;
        if (write)
            memcpy(bounce, p, bytes);
        if (submit(&ioq, c) < 0)
            return -EIO;
        if (!write)
            memcpy(p, bounce, bytes);
        p += bytes;
        lba += n;
        count -= n;
    }
    return 0;
}

static int nvme_flush(void *priv)
{
    uint32_t c[16];

    (void)priv;
    memset(c, 0, sizeof(c));
    c[0] = 0;
    c[1] = 1;
    return submit(&ioq, c);
}

static int nvme_read(void *priv, uint32_t blk, void *buf)
{
    return nvme_rw(priv, (uint64_t)blk * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK, buf, 0);
}

static int nvme_write(void *priv, uint32_t blk, const void *buf)
{
    return nvme_rw(priv, (uint64_t)blk * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK, (void *)buf, 1);
}

static bool wait_rdy(uint32_t want)
{
    unsigned i;

    for (i = 0; i < SPIN_LIMIT; i++) {
        if ((rd(R_CSTS) & 1) == want)
            return true;
        __asm__ volatile("pause" : : : "memory");
    }
    return false;
}

static bool admin(uint32_t op, uint32_t nsid, paddr_t prp, uint32_t d10, uint32_t d11)
{
    uint32_t c[16];

    memset(c, 0, sizeof(c));
    c[0] = op;
    c[1] = nsid;
    c[6] = (uint32_t)prp;
    c[7] = (uint32_t)(prp >> 32);
    c[10] = d10;
    c[11] = d11;
    return submit(&adm, c) == 0;
}

struct blkdrv *nvme_probe(void)
{
    struct pci_dev d;
    uint64_t bar;
    uint32_t cap_lo, cap_hi, stride, cmd, flbas, lbads;
    paddr_t asq, acq, isq, icq, idp;
    uint8_t *id;
    uint64_t sectors;
    unsigned i, len;

    if (!pci_find_class(PCI_CLASS_NVME, &d) || (d.bar[0] & 1))
        return NULL;
    bar = d.bar[0] & ~0xFull;
    if ((d.bar[0] & 6) == 4)
        bar |= (uint64_t)d.bar[1] << 32;
    cmd = pci_read32(d.bus, d.dev, d.fn, 0x04) & 0xFFFFu;
    pci_write32(d.bus, d.dev, d.fn, 0x04, cmd | 0x2 | 0x4 | 0x400);
    regs = vmm_map_mmio(bar, 0x2000);
    if (!regs)
        return NULL;
    cap_lo = rd(R_CAP);
    cap_hi = rd(R_CAP + 4);
    if ((cap_hi >> 16) & 0xF) {
        kprintf("nvme: controller needs pages larger than 4K\n");
        return NULL;
    }
    stride = 4u << (cap_hi & 0xF);

    wr(R_CC, 0);
    if (!wait_rdy(0))
        return NULL;
    asq = pmm_alloc_zero();
    acq = pmm_alloc_zero();
    isq = pmm_alloc_zero();
    icq = pmm_alloc_zero();
    idp = pmm_alloc_zero();
    bounce_pa = pmm_alloc_contig(2);
    if (!asq || !acq || !isq || !icq || !idp || !bounce_pa)
        return NULL;
    bounce = P2V(bounce_pa);
    id = P2V(idp);

    adm.sq = P2V(asq);
    adm.cq = P2V(acq);
    adm.sqdb = (volatile uint32_t *)(regs + 0x1000);
    adm.cqdb = (volatile uint32_t *)(regs + 0x1000 + stride);
    adm.qsz = QSZ;
    adm.phase = 1;
    ioq.sq = P2V(isq);
    ioq.cq = P2V(icq);
    ioq.sqdb = (volatile uint32_t *)(regs + 0x1000 + 2 * stride);
    ioq.cqdb = (volatile uint32_t *)(regs + 0x1000 + 3 * stride);
    ioq.qsz = QSZ;
    ioq.phase = 1;
    (void)cap_lo;

    wr(R_AQA, ((QSZ - 1) << 16) | (QSZ - 1));
    wr(R_ASQ, (uint32_t)asq);
    wr(R_ASQ + 4, (uint32_t)(asq >> 32));
    wr(R_ACQ, (uint32_t)acq);
    wr(R_ACQ + 4, (uint32_t)(acq >> 32));
    wr(R_CC, 1u | (6u << 16) | (4u << 20));
    if (!wait_rdy(1)) {
        kprintf("nvme: controller did not become ready\n");
        return NULL;
    }

    if (!admin(9, 0, 0, 7, 0) ||
        !admin(5, 0, icq, ((QSZ - 1) << 16) | 1, 1) ||
        !admin(1, 0, isq, ((QSZ - 1) << 16) | 1, (1u << 16) | 1)) {
        kprintf("nvme: queue setup failed\n");
        return NULL;
    }
    if (!admin(6, 0, idp, 1, 0))
        return NULL;
    for (i = 0; i < 40; i++)
        drv.model[i] = (char)id[24 + i];
    for (len = 40; len > 0 && (drv.model[len - 1] == ' ' || drv.model[len - 1] == 0); len--)
        ;
    drv.model[len] = 0;
    memset(id, 0, 4096);
    if (!admin(6, 1, idp, 0, 0)) {
        kprintf("nvme: namespace 1 not available\n");
        return NULL;
    }
    sectors = *(uint64_t *)id;
    flbas = id[26] & 0xF;
    lbads = (*(uint32_t *)(id + 128 + 4 * flbas) >> 16) & 0xFF;
    if (lbads != 9) {
        kprintf("nvme: only 512-byte sectors are supported\n");
        return NULL;
    }
    if (!sectors)
        return NULL;
    drv.name = name;
    drv.nsectors = sectors;
    drv.nblocks = (uint32_t)(sectors / SECTORS_PER_BLOCK > 0xFFFFFFFFu ? 0xFFFFFFFFu
                                                                      : sectors / SECTORS_PER_BLOCK);
    drv.priv = &drv;
    drv.read = nvme_read;
    drv.write = nvme_write;
    drv.rw = nvme_rw;
    drv.flush = nvme_flush;
    blk_register(&drv);
    kprintf("[ok] %s: %s, %lu MiB\n", drv.name, drv.model, drv.nsectors / 2048);
    return &drv;
}
