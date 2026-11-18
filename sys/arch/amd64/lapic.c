#include <cpu.h>
#include <mm.h>
#include <msr.h>
#include <kern.h>

#define LAPIC_PHYS      0xFEE00000UL
#define LAPIC_SVR       0x0F0
#define LAPIC_LVT_LINT0 0x350
#define LAPIC_LVT_LINT1 0x360

#define SVR_ENABLE   0x100
#define SPURIOUS_VEC 0x2F

#define LVT_EXTINT   0x700
#define LVT_NMI      0x400

static volatile uint32_t *lapic;

void lapic_init(void)
{
    uint64_t base = rdmsr(0x1B);

    if (!(base & (1UL << 11)))
        panic("LAPIC is disabled");
    if ((base & 0xFFFFF000UL) != LAPIC_PHYS)
        panic("unexpected LAPIC base: %lx", base);

    lapic = vmm_map_mmio(LAPIC_PHYS, PAGE_SIZE);

    lapic[LAPIC_SVR / 4]       = SVR_ENABLE | SPURIOUS_VEC;
    lapic[LAPIC_LVT_LINT0 / 4] = LVT_EXTINT;
    lapic[LAPIC_LVT_LINT1 / 4] = LVT_NMI;
}
