#include <power.h>
#include <kern.h>
#include <blk.h>
#include <io.h>
#include <mm.h>
#include <cpu.h>

uint64_t acpi_rsdp_phys;

struct acpi_hdr {
    char     sig[4];
    uint32_t len;
    uint8_t  rev, sum;
    char     oem[6], tab[8];
    uint32_t oem_rev, creator, creator_rev;
} __attribute__((packed));

static const uint8_t *phys(uint64_t pa)
{
    return (const uint8_t *)P2V(pa);
}

static bool sig_is(const void *p, const char *s, int n)
{
    const char *a = p;
    int i;

    for (i = 0; i < n; i++)
        if (a[i] != s[i])
            return false;
    return true;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

static const uint8_t *find_table(const char *sig)
{
    const uint8_t *rsdp, *root;
    uint64_t addr;
    bool x64;
    uint32_t len, i, entsz;

    if (!acpi_rsdp_phys)
        return NULL;
    rsdp = phys(acpi_rsdp_phys);
    if (!sig_is(rsdp, "RSD PTR ", 8))
        return NULL;
    x64 = rsdp[15] >= 2 && rd64(rsdp + 24);
    addr = x64 ? rd64(rsdp + 24) : rd32(rsdp + 16);
    root = phys(addr);
    len = rd32(root + 4);
    entsz = x64 ? 8 : 4;
    if (len < sizeof(struct acpi_hdr))
        return NULL;
    for (i = sizeof(struct acpi_hdr); i + entsz <= len; i += entsz) {
        uint64_t t = x64 ? rd64(root + i) : rd32(root + i);

        if (t && sig_is(phys(t), sig, 4))
            return phys(t);
    }
    return NULL;
}

/* Read the \_S5 sleep type out of the DSDT and write it to the PM1 control block. */
static void acpi_poweroff(void)
{
    const uint8_t *fadt = find_table("FACP"), *dsdt, *p, *end;
    uint32_t pm1a, pm1b, smi;
    uint64_t d;
    uint16_t typa = 0, typb = 0;
    bool found = false;

    if (!fadt || rd32(fadt + 4) < 72)
        return;
    d = rd32(fadt + 40);
    if (!d && rd32(fadt + 4) >= 148)
        d = rd64(fadt + 140);
    if (!d)
        return;
    dsdt = phys(d);
    if (!sig_is(dsdt, "DSDT", 4))
        return;
    smi = rd32(fadt + 48);
    pm1a = rd32(fadt + 64);
    pm1b = rd32(fadt + 68);
    if (!pm1a)
        return;
    for (p = dsdt + sizeof(struct acpi_hdr), end = dsdt + rd32(dsdt + 4) - 8; p < end; p++) {
        if (!sig_is(p, "_S5_", 4))
            continue;
        if (!(p[-1] == 0x08 || (p[-2] == 0x08 && p[-1] == '\\')))
            continue;
        if (p[4] != 0x12)
            continue;
        p += 5;
        p += ((*p & 0xC0) >> 6) + 2;
        if (*p == 0x0A)
            p++;
        typa = (uint16_t)(*p++ << 10);
        if (*p == 0x0A)
            p++;
        typb = (uint16_t)(*p << 10);
        found = true;
        break;
    }
    if (!found)
        return;
    if (!(inw((uint16_t)pm1a) & 1) && smi && fadt[52]) {
        int i;

        outb((uint16_t)smi, fadt[52]);
        for (i = 0; i < 300 && !(inw((uint16_t)pm1a) & 1); i++)
            for (volatile int w = 0; w < 100000; w++)
                ;
    }
    outw((uint16_t)pm1a, (uint16_t)(typa | 0x2000));
    if (pm1b)
        outw((uint16_t)pm1b, (uint16_t)(typb | 0x2000));
}

static __noreturn void stop(void)
{
    for (;;)
        __asm__ volatile("cli; hlt");
}

static void flush(const char *what)
{
    blk_sync_all();
    kprintf("%s\n", what);
}

__noreturn void power_halt(void)
{
    flush("System halted.");
    stop();
}

__noreturn void power_off(void)
{
    flush("Powering off.");
    __asm__ volatile("cli");
    acpi_poweroff();
    /* Emulators and hypervisors that expose a fixed shutdown port. */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    kprintf("It is now safe to turn off the power.\n");
    stop();
}

__noreturn void power_restart(void)
{
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) idt = { 0, 0 };
    int i;

    flush("Restarting.");
    __asm__ volatile("cli");
    for (i = 0; i < 100000 && (inb(0x64) & 2); i++)
        ;
    outb(0x64, 0xFE);
    for (volatile int w = 0; w < 1000000; w++)
        ;
    outb(0xCF9, 0x06);
    for (volatile int w = 0; w < 1000000; w++)
        ;
    __asm__ volatile("lidt %0; int3" : : "m"(idt));
    stop();
}
