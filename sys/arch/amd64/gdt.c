#include <cpu.h>
#include <kern.h>

struct gdt_entry {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_hi;
} __packed;

struct gdt_tss_entry {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_hi;
    uint32_t base_upper;
    uint32_t reserved;
} __packed;

struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __packed;

struct gdtr {
    uint16_t limit;
    uint64_t base;
} __packed;

#define IST_STACK_SIZE 16384

static struct {
    struct gdt_entry     e[5];
    struct gdt_tss_entry tss;
} gdt __attribute__((aligned(16)));

static struct tss tss;
static uint8_t ist1_stack[IST_STACK_SIZE] __attribute__((aligned(16)));

extern void gdt_flush(struct gdtr *g);

static void set_seg(int i, uint8_t access, uint8_t gran)
{
    gdt.e[i].limit_lo = 0;
    gdt.e[i].base_lo  = 0;
    gdt.e[i].base_mid = 0;
    gdt.e[i].access   = access;
    gdt.e[i].gran     = gran;
    gdt.e[i].base_hi  = 0;
}

void tss_set_rsp0(uint64_t rsp0)
{
    tss.rsp[0] = rsp0;
}

void gdt_init(void)
{
    struct gdtr g;
    uint64_t tb = (uint64_t)&tss;
    uint32_t tl = sizeof(tss) - 1;

    memset(&gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    set_seg(1, 0x9A, 0x20);
    set_seg(2, 0x92, 0x00);
    set_seg(3, 0xF2, 0x00);
    set_seg(4, 0xFA, 0x20);

    gdt.tss.limit_lo   = tl & 0xFFFF;
    gdt.tss.base_lo    = tb & 0xFFFF;
    gdt.tss.base_mid   = (tb >> 16) & 0xFF;
    gdt.tss.access     = 0x89;
    gdt.tss.gran       = (tl >> 16) & 0x0F;
    gdt.tss.base_hi    = (tb >> 24) & 0xFF;
    gdt.tss.base_upper = (uint32_t)(tb >> 32);

    tss.ist[0]     = (uint64_t)ist1_stack + IST_STACK_SIZE;
    tss.iomap_base = sizeof(tss);

    g.limit = sizeof(gdt) - 1;
    g.base  = (uint64_t)&gdt;
    gdt_flush(&g);
    __asm__ volatile("ltr %0" : : "r"((uint16_t)SEL_TSS));
}
