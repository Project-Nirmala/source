#include <cpu.h>
#include <proc.h>
#include <msr.h>
#include <kern.h>

struct cpu cpu0;

#define RFLAGS_MASK 0x44700UL

static void pat_init(void)
{
    uint64_t pat = rdmsr(MSR_PAT);

    pat &= ~(0xFFUL << (6 * 8));
    pat |= (1UL << (6 * 8));
    wrmsr(MSR_PAT, pat);
}

void cpu_init(void)
{
    cpu0.self = &cpu0;

    pat_init();

    wrmsr(MSR_GS_BASE, (uint64_t)&cpu0);
    wrmsr(MSR_KERNEL_GS_BASE, 0);

    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
    wrmsr(MSR_STAR, ((uint64_t)0x10 << 48) | ((uint64_t)SEL_KCODE << 32));
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, RFLAGS_MASK);
}
