#include <limine.h>
#include <mm.h>
#include <kern.h>

static uint64_t *bitmap;
static uint64_t total_pages;
static uint64_t free_pages;
static uint64_t hint;

static inline void bm_set(uint64_t pfn)   { bitmap[pfn / 64] |=  (1UL << (pfn % 64)); }
static inline void bm_clear(uint64_t pfn) { bitmap[pfn / 64] &= ~(1UL << (pfn % 64)); }
static inline bool bm_test(uint64_t pfn)  { return bitmap[pfn / 64] & (1UL << (pfn % 64)); }

void pmm_init(struct limine_memmap_response *mm)
{
    uint64_t i, top = 0, bytes, bm_pages, bm_phys = 0, pfn;

    for (i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->base + e->length > top)
            top = e->base + e->length;
    }
    total_pages = top >> PAGE_SHIFT;
    bytes       = ((total_pages + 63) / 64) * 8;
    bm_pages    = PAGE_ALIGN_UP(bytes) >> PAGE_SHIFT;

    for (i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->base >= 0x100000 &&
            e->length >= bm_pages * PAGE_SIZE) {
            bm_phys = e->base;
            break;
        }
    }
    if (!bm_phys)
        panic("pmm: no usable region for bitmap");

    bitmap = P2V(bm_phys);
    memset(bitmap, 0xFF, bm_pages * PAGE_SIZE);

    for (i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;
        for (pfn = e->base >> PAGE_SHIFT; pfn < (e->base + e->length) >> PAGE_SHIFT; pfn++) {
            bm_clear(pfn);
            free_pages++;
        }
    }

    for (pfn = bm_phys >> PAGE_SHIFT; pfn < (bm_phys >> PAGE_SHIFT) + bm_pages; pfn++)
        if (!bm_test(pfn)) { bm_set(pfn); free_pages--; }
    for (pfn = 0; pfn < 256 && pfn < total_pages; pfn++)
        if (!bm_test(pfn)) { bm_set(pfn); free_pages--; }
}

paddr_t pmm_alloc(void)
{
    uint64_t words = (total_pages + 63) / 64, n;

    for (n = 0; n < words; n++) {
        uint64_t w = (hint + n) % words;
        if (bitmap[w] != ~0UL) {
            int b = __builtin_ctzl(~bitmap[w]);
            bitmap[w] |= 1UL << b;
            free_pages--;
            hint = w;
            return (w * 64 + b) << PAGE_SHIFT;
        }
    }
    return 0;
}

paddr_t pmm_alloc_zero(void)
{
    paddr_t pa = pmm_alloc();
    if (pa)
        memset(P2V(pa), 0, PAGE_SIZE);
    return pa;
}

paddr_t pmm_alloc_contig(unsigned pages)
{
    uint64_t pfn, run = 0, start = 0, i;

    for (pfn = 256; pfn < total_pages; pfn++) {
        if (bm_test(pfn)) {
            run = 0;
            continue;
        }
        if (run == 0)
            start = pfn;
        if (++run == pages) {
            for (i = 0; i < pages; i++)
                bm_set(start + i);
            free_pages -= pages;
            memset(P2V(start << PAGE_SHIFT), 0, (size_t)pages * PAGE_SIZE);
            return start << PAGE_SHIFT;
        }
    }
    return 0;
}

void pmm_free(paddr_t pa)
{
    uint64_t pfn = pa >> PAGE_SHIFT;

    if ((pa & (PAGE_SIZE - 1)) || pfn >= total_pages)
        panic("pmm_free: invalid address %lx", pa);
    if (!bm_test(pfn))
        panic("pmm_free: double free %lx", pa);
    bm_clear(pfn);
    free_pages++;
    hint = pfn / 64;
}

uint64_t pmm_free_pages(void)  { return free_pages; }
uint64_t pmm_total_pages(void) { return total_pages; }
