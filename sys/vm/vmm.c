#include <limine.h>
#include <mm.h>
#include <kern.h>
#include <msr.h>
#include <swap.h>

#define HEAP_BASE 0xFFFFC00000000000UL

uint64_t kernel_pml4;
static bool frozen;

extern char __kernel_start[], __text_start[], __text_end[];
extern char __rodata_start[], __rodata_end[], __data_start[], __kernel_end[];

static uint64_t *table_of(uint64_t e) { return P2V(e & PTE_ADDR); }

static uint64_t *next_table(uint64_t *t, int idx, bool create, bool user)
{
    uint64_t e = t[idx];

    if (!(e & PTE_P)) {
        uint64_t pa;
        if (!create)
            return NULL;
        pa = pmm_alloc_zero();
        if (!pa)
            return NULL;
        e = pa | PTE_P | PTE_RW | (user ? PTE_US : 0);
        t[idx] = e;
    } else if (e & PTE_PS) {
        panic("vmm: huge page in the way of page walk");
    } else if (user && !(e & PTE_US)) {
        e |= PTE_US;
        t[idx] = e;
    }
    return table_of(e);
}

static uint64_t *walk(uint64_t pml4, uint64_t va, bool create, bool user, bool want_pd)
{
    uint64_t *t = P2V(pml4);
    int i4 = (va >> 39) & 511, i3 = (va >> 30) & 511;
    int i2 = (va >> 21) & 511, i1 = (va >> 12) & 511;

    if (create && frozen && pml4 == kernel_pml4 && i4 >= 256 && !(t[i4] & PTE_P))
        panic("vmm: kernel PML4 entry %d created after freeze", i4);

    t = next_table(t, i4, create, user);
    if (!t) return NULL;
    t = next_table(t, i3, create, user);
    if (!t) return NULL;
    if (want_pd)
        return &t[i2];
    t = next_table(t, i2, create, user);
    if (!t) return NULL;
    return &t[i1];
}

int vmm_map(uint64_t pml4, uint64_t va, uint64_t pa, uint64_t flags)
{
    uint64_t *pte = walk(pml4, va, true, flags & PTE_US, false);

    if (!pte || (*pte & PTE_P))
        return -1;
    *pte = (pa & PTE_ADDR) | flags | PTE_P;
    __asm__ volatile("invlpg (%0)" : : "r"(va) : "memory");
    return 0;
}

uint64_t vmm_unmap(uint64_t pml4, uint64_t va)
{
    uint64_t *pte = walk(pml4, va, false, false, false);
    uint64_t pa;

    if (!pte)
        return 0;
    if (PTE_IS_SWAPPED(*pte)) {
        swap_free(PTE_SWAP_SLOT(*pte));
        *pte = 0;
        return 0;
    }
    if (!(*pte & PTE_P))
        return 0;
    pa = *pte & PTE_ADDR;
    *pte = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(va) : "memory");
    return pa;
}

uint64_t *vmm_pte(uint64_t pml4, uint64_t va)
{
    return walk(pml4, va, false, false, false);
}

bool vmm_user_probe(uint64_t pml4, uint64_t va, uint64_t *flags)
{
    uint64_t *pte = walk(pml4, va, false, false, false);

    if (!pte || (!(*pte & PTE_P) && !PTE_IS_SWAPPED(*pte)))
        return false;
    if (flags)
        *flags = *pte & PTE_SWAPKEEP;
    return true;
}

void vmm_flush_va(uint64_t pml4, uint64_t va)
{
    uint64_t cr3;

    __asm__ volatile("movq %%cr3, %0" : "=r"(cr3));
    if ((cr3 & PTE_ADDR) == pml4)
        __asm__ volatile("invlpg (%0)" : : "r"(va) : "memory");
}

uint64_t *vmm_next_user_pte(uint64_t pml4, uint64_t *cursor, unsigned want)
{
    uint64_t va = *cursor & ~(PAGE_SIZE - 1);
    uint64_t *t4 = P2V(pml4);

    while (va < USER_TOP) {
        uint64_t e = t4[(va >> 39) & 511];
        uint64_t *t;

        if (!(e & PTE_P)) {
            va = (va | 0x7FFFFFFFFFUL) + 1;
            continue;
        }
        t = table_of(e);
        e = t[(va >> 30) & 511];
        if (!(e & PTE_P) || (e & PTE_PS)) {
            va = (va | 0x3FFFFFFFUL) + 1;
            continue;
        }
        t = table_of(e);
        e = t[(va >> 21) & 511];
        if (!(e & PTE_P) || (e & PTE_PS)) {
            va = (va | 0x1FFFFFUL) + 1;
            continue;
        }
        t = table_of(e);
        e = t[(va >> 12) & 511];
        if (((want & VMM_PRESENT) && (e & PTE_P)) || ((want & VMM_SWAPPED) && PTE_IS_SWAPPED(e))) {
            *cursor = va;
            return &t[(va >> 12) & 511];
        }
        va += PAGE_SIZE;
    }
    *cursor = 0;
    return NULL;
}

bool vmm_translate(uint64_t pml4, uint64_t va, uint64_t *pa, uint64_t *flags)
{
    uint64_t *t = P2V(pml4), e;

    e = t[(va >> 39) & 511];
    if (!(e & PTE_P)) return false;
    t = table_of(e);
    e = t[(va >> 30) & 511];
    if (!(e & PTE_P)) return false;
    if (e & PTE_PS) {
        if (pa)    *pa = (e & 0x000FFFFFC0000000UL) | (va & 0x3FFFFFFFUL);
        if (flags) *flags = e & ~PTE_ADDR;
        return true;
    }
    t = table_of(e);
    e = t[(va >> 21) & 511];
    if (!(e & PTE_P)) return false;
    if (e & PTE_PS) {
        if (pa)    *pa = (e & 0x000FFFFFFFE00000UL) | (va & 0x1FFFFFUL);
        if (flags) *flags = e & ~PTE_ADDR;
        return true;
    }
    t = table_of(e);
    e = t[(va >> 12) & 511];
    if (!(e & PTE_P)) return false;
    if (pa)    *pa = (e & PTE_ADDR) | (va & 0xFFF);
    if (flags) *flags = e & ~PTE_ADDR;
    return true;
}

static void map_range(uint64_t pml4, uint64_t va, uint64_t pa, uint64_t len, uint64_t flags)
{
    while (len) {
        if (!(va & 0x1FFFFF) && !(pa & 0x1FFFFF) && len >= 0x200000) {
            uint64_t *pde = walk(pml4, va, true, false, true);
            if (!pde) panic("vmm: out of memory in map_range");
            if (*pde & PTE_P) panic("vmm: map_range 2M overlap at %lx", va);
            *pde = pa | flags | PTE_P | PTE_PS;
            va += 0x200000; pa += 0x200000; len -= 0x200000;
        } else {
            uint64_t old, oflags;
            if (vmm_translate(pml4, va, &old, &oflags)) {
                if ((old & PTE_ADDR) != pa)
                    panic("vmm: map_range conflict at %lx", va);
            } else if (vmm_map(pml4, va, pa, flags) < 0) {
                panic("vmm: out of memory in map_range");
            }
            va += PAGE_SIZE; pa += PAGE_SIZE; len -= PAGE_SIZE;
        }
    }
}

static void map_section(uint64_t pml4, char *s, char *e, uint64_t kvirt, uint64_t kphys,
                        uint64_t flags)
{
    uint64_t va;
    for (va = (uint64_t)s; va < (uint64_t)e; va += PAGE_SIZE)
        if (vmm_map(pml4, va, va - kvirt + kphys, flags) < 0)
            panic("vmm: failed to map kernel page %lx", va);
}

void vmm_init(struct limine_memmap_response *mm, uint64_t kvirt, uint64_t kphys)
{
    uint64_t i;
    uint64_t *top;

    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_NXE);

    kernel_pml4 = pmm_alloc_zero();
    if (!kernel_pml4)
        panic("vmm: out of memory");

    for (i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        uint64_t base, end;

        switch (e->type) {
        case LIMINE_MEMMAP_USABLE:
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
        case LIMINE_MEMMAP_RESERVED_MAPPED:
            base = PAGE_ALIGN_DOWN(e->base);
            end  = PAGE_ALIGN_UP(e->base + e->length);
            map_range(kernel_pml4, hhdm_offset + base, base, end - base, PTE_RW | PTE_NX);
            break;
        default:
            break;
        }
    }

    map_section(kernel_pml4, __kernel_start, __text_start,   kvirt, kphys, PTE_NX);
    map_section(kernel_pml4, __text_start,   __text_end,     kvirt, kphys, 0);
    map_section(kernel_pml4, __rodata_start, __rodata_end,   kvirt, kphys, PTE_NX);
    map_section(kernel_pml4, __data_start,   __kernel_end,   kvirt, kphys, PTE_RW | PTE_NX);

    top = P2V(kernel_pml4);
    next_table(top, (hhdm_offset >> 39) & 511, true, false);
    next_table(top, (HEAP_BASE >> 39) & 511, true, false);
    next_table(top, (kvirt >> 39) & 511, true, false);
    frozen = true;

    vmm_switch(kernel_pml4);
}

void vmm_switch(uint64_t pml4)
{
    __asm__ volatile("movq %0, %%cr3" : : "r"(pml4) : "memory");
}

uint64_t vmm_new_user(void)
{
    uint64_t pml4 = pmm_alloc_zero();
    uint64_t *n, *k = P2V(kernel_pml4);
    int i;

    if (!pml4)
        return 0;
    n = P2V(pml4);
    for (i = 256; i < 512; i++)
        n[i] = k[i];
    return pml4;
}

uint64_t vmm_copy_user(uint64_t src)
{
    uint64_t dst = vmm_new_user();
    uint64_t *p4 = P2V(src);
    int i4, i3, i2, i1;

    if (!dst)
        return 0;
    for (i4 = 0; i4 < 256; i4++) {
        uint64_t *p3;
        if (!(p4[i4] & PTE_P)) continue;
        p3 = table_of(p4[i4]);
        for (i3 = 0; i3 < 512; i3++) {
            uint64_t *p2;
            if (!(p3[i3] & PTE_P)) continue;
            p2 = table_of(p3[i3]);
            for (i2 = 0; i2 < 512; i2++) {
                uint64_t *p1;
                if (!(p2[i2] & PTE_P)) continue;
                p1 = table_of(p2[i2]);
                for (i1 = 0; i1 < 512; i1++) {
                    uint64_t e = p1[i1], va, npa;
                    if (!(e & PTE_P) && !PTE_IS_SWAPPED(e)) continue;
                    va = ((uint64_t)i4 << 39) | ((uint64_t)i3 << 30) |
                         ((uint64_t)i2 << 21) | ((uint64_t)i1 << 12);
                    if (PTE_IS_SWAPPED(e)) {
                        uint64_t *dpte;

                        swap_dup(PTE_SWAP_SLOT(e));
                        dpte = walk(dst, va, true, true, false);
                        if (!dpte) {
                            swap_free(PTE_SWAP_SLOT(e));
                            goto fail;
                        }
                        *dpte = e;
                        continue;
                    }
                    npa = pmm_alloc();
                    if (!npa)
                        goto fail;
                    memcpy(P2V(npa), P2V(e & PTE_ADDR), PAGE_SIZE);
                    if (vmm_map(dst, va, npa, e & (PTE_RW | PTE_US | PTE_NX)) < 0) {
                        pmm_free(npa);
                        goto fail;
                    }
                }
            }
        }
    }
    return dst;

fail:
    vmm_destroy_user(dst);
    return 0;
}

void vmm_destroy_user(uint64_t pml4)
{
    uint64_t *p4 = P2V(pml4);
    int i4, i3, i2, i1;

    for (i4 = 0; i4 < 256; i4++) {
        uint64_t *p3;
        if (!(p4[i4] & PTE_P)) continue;
        p3 = table_of(p4[i4]);
        for (i3 = 0; i3 < 512; i3++) {
            uint64_t *p2;
            if (!(p3[i3] & PTE_P)) continue;
            p2 = table_of(p3[i3]);
            for (i2 = 0; i2 < 512; i2++) {
                uint64_t *p1;
                if (!(p2[i2] & PTE_P)) continue;
                p1 = table_of(p2[i2]);
                for (i1 = 0; i1 < 512; i1++) {
                    if (p1[i1] & PTE_P)
                        pmm_free(p1[i1] & PTE_ADDR);
                    else if (PTE_IS_SWAPPED(p1[i1]))
                        swap_free(PTE_SWAP_SLOT(p1[i1]));
                }
                pmm_free(p2[i2] & PTE_ADDR);
            }
            pmm_free(p3[i3] & PTE_ADDR);
        }
        pmm_free(p4[i4] & PTE_ADDR);
    }
    pmm_free(pml4);
}

void *vmm_map_mmio(uint64_t pa, size_t len)
{
    uint64_t start = PAGE_ALIGN_DOWN(pa), end = PAGE_ALIGN_UP(pa + len), p;

    for (p = start; p < end; p += PAGE_SIZE)
        if (!vmm_translate(kernel_pml4, hhdm_offset + p, NULL, NULL) &&
            vmm_map(kernel_pml4, hhdm_offset + p, p, PTE_RW | PTE_PWT | PTE_PCD | PTE_NX) < 0)
            panic("vmm_map_mmio: out of memory");
    return P2V(pa);
}

/*
 * Same as vmm_map_mmio(), but selects the Write-Combining PAT slot
 * (PAT=1, PCD=1, PWT=0; see pat_init() in cpu.c) instead of strict UC.
 * Use this only for linear framebuffers: writes may be buffered/merged
 * and are not guaranteed to land in a fixed order, which is fine for
 * pixel data but unsafe for device control/status registers.
 */
void *vmm_map_mmio_wc(uint64_t pa, size_t len)
{
    uint64_t start = PAGE_ALIGN_DOWN(pa), end = PAGE_ALIGN_UP(pa + len), p;

    for (p = start; p < end; p += PAGE_SIZE)
        if (!vmm_translate(kernel_pml4, hhdm_offset + p, NULL, NULL) &&
            vmm_map(kernel_pml4, hhdm_offset + p, p, PTE_RW | PTE_PAT | PTE_PCD | PTE_NX) < 0)
            panic("vmm_map_mmio_wc: out of memory");
    return P2V(pa);
}
