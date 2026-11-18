#include <mm.h>
#include <kern.h>

#define HEAP_BASE  0xFFFFC00000000000UL
#define HEAP_LIMIT (HEAP_BASE + (1UL << 30))
#define HEAP_GROW  (64 * 1024)
#define MAGIC      0x4E49524DU

struct blk {
    uint64_t size;
    uint64_t prev_size;
    uint32_t magic;
    uint32_t free;
    uint64_t pad;
};
#define HDR sizeof(struct blk)

static uint8_t    *heap_top;
static struct blk *last;

static inline struct blk *next_of(struct blk *b)
{
    uint8_t *n = (uint8_t *)b + b->size;
    return n < heap_top ? (struct blk *)n : NULL;
}

static bool grow(size_t need)
{
    size_t bytes = PAGE_ALIGN_UP(need > HEAP_GROW ? need : HEAP_GROW), off;
    struct blk *b;

    if ((uint64_t)heap_top + bytes > HEAP_LIMIT)
        return false;
    for (off = 0; off < bytes; off += PAGE_SIZE) {
        paddr_t pa = pmm_alloc();
        if (!pa || vmm_map(kernel_pml4, (uint64_t)heap_top + off, pa, PTE_RW | PTE_NX) < 0)
            return false;
    }
    b = (struct blk *)heap_top;
    b->size = bytes;
    b->prev_size = last ? last->size : 0;
    b->magic = MAGIC;
    b->free = 1;
    heap_top += bytes;
    if (last && last->free) {
        last->size += b->size;
    } else {
        last = b;
    }
    return true;
}

void kheap_init(void)
{
    heap_top = (uint8_t *)HEAP_BASE;
    last = NULL;
    if (!grow(HEAP_GROW))
        panic("kheap: init failed");
}

void *kmalloc(size_t n)
{
    size_t need;
    struct blk *b;
    int tries;

    n = (n + 15) & ~(size_t)15;
    if (n == 0) n = 16;
    need = n + HDR;

    for (tries = 0; tries < 2; tries++) {
        for (b = (struct blk *)HEAP_BASE; b; b = next_of(b)) {
            if (b->magic != MAGIC)
                panic("kheap: corrupt header at %p", b);
            if (!b->free || b->size < need)
                continue;
            if (b->size - need >= HDR + 16) {
                struct blk *r = (struct blk *)((uint8_t *)b + need), *nx;
                r->size = b->size - need;
                r->prev_size = need;
                r->magic = MAGIC;
                r->free = 1;
                b->size = need;
                nx = next_of(r);
                if (nx) nx->prev_size = r->size;
                if (b == last) last = r;
            }
            b->free = 0;
            return b + 1;
        }
        if (!grow(need))
            return NULL;
    }
    return NULL;
}

void *kzalloc(size_t n)
{
    void *p = kmalloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void kfree(void *p)
{
    struct blk *b, *nx;

    if (!p)
        return;
    b = (struct blk *)p - 1;
    if (b->magic != MAGIC)
        panic("kfree: corrupt header %p", p);
    if (b->free)
        panic("kfree: double free %p", p);
    b->free = 1;

    nx = next_of(b);
    if (nx && nx->free) {
        b->size += nx->size;
        if (nx == last) last = b;
    }
    if (b->prev_size) {
        struct blk *pv = (struct blk *)((uint8_t *)b - b->prev_size);
        if (pv->free) {
            pv->size += b->size;
            if (b == last) last = pv;
            b = pv;
        }
    }
    nx = next_of(b);
    if (nx) nx->prev_size = b->size;
}
