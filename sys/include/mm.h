#ifndef _NIRMALA_MM_H
#define _NIRMALA_MM_H

#include <types.h>

struct limine_memmap_response;

#define PAGE_SIZE   4096UL
#define PAGE_SHIFT  12
#define PAGE_ALIGN_UP(x)   (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(x) ((x) & ~(PAGE_SIZE - 1))

#define PTE_P    (1UL << 0)
#define PTE_RW   (1UL << 1)
#define PTE_US   (1UL << 2)
#define PTE_PWT  (1UL << 3)
#define PTE_PCD  (1UL << 4)
#define PTE_A    (1UL << 5)
#define PTE_PS   (1UL << 7)
#define PTE_SWAP (1UL << 9)
#define PTE_PAT  (1UL << 7)  /* same bit as PTE_PS; only meaningful on 4K PTEs */
#define PTE_NX   (1UL << 63)
#define PTE_ADDR 0x000FFFFFFFFFF000UL
#define PTE_SWAPKEEP (PTE_RW | PTE_US | PTE_NX)

#define PTE_IS_SWAPPED(e)   (!((e) & PTE_P) && ((e) & PTE_SWAP))
#define PTE_SWAP_SLOT(e)    (((e) & PTE_ADDR) >> PAGE_SHIFT)
#define SWAP_PTE(slot, e)   (((uint64_t)(slot) << PAGE_SHIFT) | PTE_SWAP | ((e) & PTE_SWAPKEEP))

#define VMM_PRESENT 1u
#define VMM_SWAPPED 2u

#define USER_TOP         0x0000800000000000UL
#define USER_BASE        0x0000000000400000UL
#define USER_STACK_TOP   0x00007FFFFFFFF000UL
#define USER_STACK_PAGES 64

extern uint64_t hhdm_offset;
#define P2V(pa) ((void *)(hhdm_offset + (uint64_t)(pa)))

void     pmm_init(struct limine_memmap_response *mm);
paddr_t  pmm_alloc(void);
paddr_t  pmm_try_alloc(void);
void     pmm_set_reclaim(int (*fn)(void));
paddr_t  pmm_alloc_zero(void);
paddr_t  pmm_alloc_contig(unsigned pages);
void     pmm_free(paddr_t pa);
uint64_t pmm_free_pages(void);
uint64_t pmm_total_pages(void);

extern uint64_t kernel_pml4;
void     vmm_init(struct limine_memmap_response *mm, uint64_t kvirt, uint64_t kphys);
int      vmm_map(uint64_t pml4, uint64_t va, uint64_t pa, uint64_t flags);
uint64_t vmm_unmap(uint64_t pml4, uint64_t va);
bool     vmm_translate(uint64_t pml4, uint64_t va, uint64_t *pa, uint64_t *flags);
uint64_t vmm_new_user(void);
uint64_t vmm_copy_user(uint64_t src_pml4);
void     vmm_destroy_user(uint64_t pml4);
void     vmm_switch(uint64_t pml4);
uint64_t *vmm_pte(uint64_t pml4, uint64_t va);
bool     vmm_user_probe(uint64_t pml4, uint64_t va, uint64_t *flags);
uint64_t *vmm_next_user_pte(uint64_t pml4, uint64_t *cursor, unsigned want);
void     vmm_flush_va(uint64_t pml4, uint64_t va);
void    *vmm_map_mmio(uint64_t pa, size_t len);
void    *vmm_map_mmio_wc(uint64_t pa, size_t len);

void  kheap_init(void);
void *kmalloc(size_t n);
void *kzalloc(size_t n);
void  kfree(void *p);

#endif
