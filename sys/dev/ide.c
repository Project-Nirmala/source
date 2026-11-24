#include <blk.h>
#include <io.h>
#include <kern.h>
#include <sys/errno.h>

#define SECTORS_PER_BLOCK (LEMON_BSIZE / 512)
#define IDE_MAX    4
#define SPIN_LIMIT 4000000u
#define CHUNK      128u

#define R_DATA  0
#define R_COUNT 2
#define R_LBA0  3
#define R_LBA1  4
#define R_LBA2  5
#define R_DRIVE 6
#define R_CMD   7

#define ST_ERR 0x01
#define ST_DRQ 0x08
#define ST_DF  0x20
#define ST_BSY 0x80

#define CMD_READ      0x20
#define CMD_WRITE     0x30
#define CMD_READ_EXT  0x24
#define CMD_WRITE_EXT 0x34
#define CMD_FLUSH     0xE7
#define CMD_FLUSH_EXT 0xEA
#define CMD_IDENTIFY  0xEC

struct ide {
    struct blkdrv drv;
    uint16_t io, ctl;
    uint8_t  slave;
    bool     lba48;
    char     name[8];
};

static struct ide disks[IDE_MAX];
static unsigned ndisks;

static void insw(uint16_t port, void *buf, unsigned n)
{
    __asm__ volatile("rep insw" : "+D"(buf), "+c"(n) : "d"(port) : "memory");
}

static void outsw(uint16_t port, const void *buf, unsigned n)
{
    __asm__ volatile("rep outsw" : "+S"(buf), "+c"(n) : "d"(port) : "memory");
}

static void delay400(struct ide *d)
{
    int i;

    for (i = 0; i < 4; i++)
        (void)inb(d->ctl);
}

static int wait_bsy(struct ide *d)
{
    unsigned i;

    for (i = 0; i < SPIN_LIMIT; i++)
        if (!(inb(d->ctl) & ST_BSY))
            return 0;
    return -1;
}

static int wait_drq(struct ide *d)
{
    unsigned i;
    uint8_t s;

    for (i = 0; i < SPIN_LIMIT; i++) {
        s = inb(d->ctl);
        if (s & (ST_ERR | ST_DF))
            return -1;
        if (!(s & ST_BSY) && (s & ST_DRQ))
            return 0;
    }
    return -1;
}

static void select(struct ide *d, uint8_t extra)
{
    outb((uint16_t)(d->io + R_DRIVE), (uint8_t)(0xE0 | (d->slave << 4) | extra));
    delay400(d);
}

static int ide_rw(void *priv, uint64_t lba, uint32_t count, void *buf, int write)
{
    struct ide *d = priv;
    uint8_t *p = buf;

    while (count) {
        uint32_t n = count > CHUNK ? CHUNK : count, i;

        if (wait_bsy(d) < 0)
            return -EIO;
        if (d->lba48) {
            select(d, 0);
            outb((uint16_t)(d->io + R_COUNT), (uint8_t)(n >> 8));
            outb((uint16_t)(d->io + R_LBA0), (uint8_t)(lba >> 24));
            outb((uint16_t)(d->io + R_LBA1), (uint8_t)(lba >> 32));
            outb((uint16_t)(d->io + R_LBA2), (uint8_t)(lba >> 40));
            outb((uint16_t)(d->io + R_COUNT), (uint8_t)n);
            outb((uint16_t)(d->io + R_LBA0), (uint8_t)lba);
            outb((uint16_t)(d->io + R_LBA1), (uint8_t)(lba >> 8));
            outb((uint16_t)(d->io + R_LBA2), (uint8_t)(lba >> 16));
            outb((uint16_t)(d->io + R_CMD), write ? CMD_WRITE_EXT : CMD_READ_EXT);
        } else {
            if (lba + n > 0x0FFFFFFFu)
                return -EIO;
            select(d, (uint8_t)((lba >> 24) & 0x0F));
            outb((uint16_t)(d->io + R_COUNT), (uint8_t)n);
            outb((uint16_t)(d->io + R_LBA0), (uint8_t)lba);
            outb((uint16_t)(d->io + R_LBA1), (uint8_t)(lba >> 8));
            outb((uint16_t)(d->io + R_LBA2), (uint8_t)(lba >> 16));
            outb((uint16_t)(d->io + R_CMD), write ? CMD_WRITE : CMD_READ);
        }
        delay400(d);
        for (i = 0; i < n; i++) {
            if (wait_drq(d) < 0)
                return -EIO;
            if (write)
                outsw(d->io + R_DATA, p, 256);
            else
                insw(d->io + R_DATA, p, 256);
            p += 512;
        }
        if (write) {
            delay400(d);
            if (wait_bsy(d) < 0 || (inb(d->ctl) & (ST_ERR | ST_DF)))
                return -EIO;
        }
        lba += n;
        count -= n;
    }
    return 0;
}

static int ide_flush(void *priv)
{
    struct ide *d = priv;

    select(d, 0);
    outb((uint16_t)(d->io + R_CMD), d->lba48 ? CMD_FLUSH_EXT : CMD_FLUSH);
    delay400(d);
    return wait_bsy(d);
}

static int ide_read(void *priv, uint32_t blk, void *buf)
{
    return ide_rw(priv, (uint64_t)blk * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK, buf, 0);
}

static int ide_write(void *priv, uint32_t blk, const void *buf)
{
    return ide_rw(priv, (uint64_t)blk * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK, (void *)buf, 1);
}

static bool ide_attach(uint16_t io, uint16_t ctl, uint8_t slave)
{
    struct ide *d = &disks[ndisks];
    uint16_t id[256];
    uint64_t sectors;
    unsigned i, len;

    d->io = io;
    d->ctl = ctl;
    d->slave = slave;
    select(d, 0);
    outb((uint16_t)(io + R_COUNT), 0);
    outb((uint16_t)(io + R_LBA0), 0);
    outb((uint16_t)(io + R_LBA1), 0);
    outb((uint16_t)(io + R_LBA2), 0);
    outb((uint16_t)(io + R_CMD), CMD_IDENTIFY);
    delay400(d);
    if (inb(ctl) == 0 || inb(ctl) == 0xFF)
        return false;
    if (wait_bsy(d) < 0)
        return false;
    if (inb((uint16_t)(io + R_LBA1)) || inb((uint16_t)(io + R_LBA2)))
        return false;
    if (wait_drq(d) < 0)
        return false;
    insw(io + R_DATA, id, 256);
    d->lba48 = (id[83] & (1u << 10)) != 0;
    if (d->lba48)
        sectors = (uint64_t)id[100] | (uint64_t)id[101] << 16 | (uint64_t)id[102] << 32 | (uint64_t)id[103] << 48;
    else
        sectors = (uint64_t)id[60] | (uint64_t)id[61] << 16;
    if (!sectors)
        return false;
    for (i = 0; i < 20; i++) {
        d->drv.model[2 * i] = (char)(id[27 + i] >> 8);
        d->drv.model[2 * i + 1] = (char)(id[27 + i] & 0xFF);
    }
    for (len = 40; len > 0 && (d->drv.model[len - 1] == ' ' || d->drv.model[len - 1] == 0); len--)
        ;
    d->drv.model[len] = 0;
    d->name[0] = 'h';
    d->name[1] = 'd';
    d->name[2] = (char)('a' + ndisks);
    d->name[3] = 0;
    d->drv.name = d->name;
    d->drv.nsectors = sectors;
    d->drv.nblocks = (uint32_t)(sectors / SECTORS_PER_BLOCK > 0xFFFFFFFFu ? 0xFFFFFFFFu
                                                                         : sectors / SECTORS_PER_BLOCK);
    d->drv.priv = d;
    d->drv.read = ide_read;
    d->drv.write = ide_write;
    d->drv.rw = ide_rw;
    d->drv.flush = ide_flush;
    return true;
}

struct blkdrv *ide_probe(void)
{
    static const uint16_t bases[2][2] = { { 0x1F0, 0x3F6 }, { 0x170, 0x376 } };
    struct blkdrv *first = NULL;
    unsigned c, s;

    for (c = 0; c < 2; c++) {
        if (inb(bases[c][1]) == 0xFF)
            continue;
        outb(bases[c][1], 0x02);
        for (s = 0; s < 2 && ndisks < IDE_MAX; s++) {
            if (!ide_attach(bases[c][0], bases[c][1], (uint8_t)s))
                continue;
            blk_register(&disks[ndisks].drv);
            kprintf("[ok] %s: %s, %lu MiB\n", disks[ndisks].name, disks[ndisks].drv.model,
                    disks[ndisks].drv.nsectors / 2048);
            if (!first)
                first = &disks[ndisks].drv;
            ndisks++;
        }
    }
    return first;
}
