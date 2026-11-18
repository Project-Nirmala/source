#include <fpu.h>
#include <kern.h>

struct fpu_state {
    uint8_t b[FPU_AREA_SIZE];
} __attribute__((aligned(16)));

static struct fpu_state template_state;

void fpu_init(void)
{
    uint64_t cr0, cr4;
    uint32_t mxcsr = 0x1F80;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1UL << 2);
    cr0 |= 1UL << 1;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1UL << 9) | (1UL << 10);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    __asm__ volatile("fninit");
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
    __asm__ volatile("fxsave %0" : "=m"(template_state) : : "memory");
}

void *fpu_align(void *raw)
{
    return (void *)(((uintptr_t)raw + 15) & ~(uintptr_t)15);
}

void fpu_save(void *area)
{
    __asm__ volatile("fxsave %0" : "=m"(*(struct fpu_state *)area) : : "memory");
}

void fpu_restore(const void *area)
{
    __asm__ volatile("fxrstor %0" : : "m"(*(const struct fpu_state *)area) : "memory");
}

void fpu_reset(void *area)
{
    memcpy(area, &template_state, FPU_AREA_SIZE);
}
