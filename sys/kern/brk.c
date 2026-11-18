#include <proc.h>
#include <mm.h>
#include <kern.h>

#define HEAP_MAX 0x10000000UL

long proc_brk(struct proc *p, uint64_t addr)
{
    uint64_t old_top = PAGE_ALIGN_UP(p->brk), new_top, va;
    paddr_t pa;

    if (addr == 0 || addr < p->brk_start || addr > p->brk_start + HEAP_MAX)
        return (long)p->brk;
    new_top = PAGE_ALIGN_UP(addr);
    if (new_top > old_top) {
        for (va = old_top; va < new_top; va += PAGE_SIZE) {
            pa = pmm_alloc_zero();
            if (!pa || vmm_map(p->pml4, va, pa, PTE_RW | PTE_US | PTE_NX) < 0) {
                if (pa)
                    pmm_free(pa);
                while (va > old_top) {
                    va -= PAGE_SIZE;
                    pa = vmm_unmap(p->pml4, va);
                    if (pa)
                        pmm_free(pa);
                }
                return (long)p->brk;
            }
        }
    } else {
        for (va = new_top; va < old_top; va += PAGE_SIZE) {
            pa = vmm_unmap(p->pml4, va);
            if (pa)
                pmm_free(pa);
        }
    }
    p->brk = addr;
    return (long)addr;
}
