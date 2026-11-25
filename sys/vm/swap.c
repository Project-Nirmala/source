#include <swap.h>
#include <blk.h>
#include <mm.h>
#include <kern.h>
#include <proc.h>
#include <sys/errno.h>

#define SWAP_MAGIC        "SWAPSPACE2"
#define SWAP_MAGIC_OFF    4086
#define SWAP_VERSION_OFF  1024
#define SWAP_LAST_OFF     1028
#define SWAP_BADPAGES_OFF 1032
#define SWAP_MAX_PAGES    (1u << 22)
#define SWAP_MAX_ERRORS   8u
#define MAP_RESERVED      0xFFu

static struct {
    bool           active;
    bool           evict_ok;
    bool           busy;
    struct blkdrv *drv;
    uint64_t       start;
    uint64_t       nsect;
    uint32_t       npages;
    uint32_t       used;
    uint32_t       bad;
    uint32_t       errors;
    uint32_t       hint;
    uint32_t       hand_proc;
    uint8_t       *map;
} sw;

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int slot_io(uint32_t slot, void *buf, int write)
{
    uint64_t off = (sw.start + (uint64_t)slot * SWAP_PAGE_SECTORS) * 512;

    return blk_rw_bytes(sw.drv, off, buf, PAGE_SIZE, write);
}

static uint32_t slots_free(void)
{
    return sw.npages - 1 - sw.used - sw.bad;
}

static uint32_t slot_alloc(void)
{
    uint32_t i, s;

    for (i = 0; i < sw.npages; i++) {
        s = (sw.hint + i) % sw.npages;
        if (s && sw.map[s] == 0) {
            sw.map[s] = 1;
            sw.used++;
            sw.hint = s + 1;
            return s;
        }
    }
    return 0;
}

void swap_free(uint64_t slot)
{
    if (!sw.map || slot == 0 || slot >= sw.npages || sw.map[slot] == 0 || sw.map[slot] == MAP_RESERVED)
        return;
    if (--sw.map[slot] == 0)
        sw.used--;
}

void swap_dup(uint64_t slot)
{
    if (!sw.map || slot == 0 || slot >= sw.npages || sw.map[slot] == 0 || sw.map[slot] >= MAP_RESERVED - 1)
        return;
    sw.map[slot]++;
}

bool swap_active(void)
{
    return sw.active;
}

bool swap_overlaps(const struct blkdrv *drv, uint64_t start, uint64_t nsect)
{
    return sw.active && sw.drv == drv && start < sw.start + sw.nsect && sw.start < start + nsect;
}

uint64_t swap_total_bytes(void)
{
    return sw.active ? (uint64_t)(sw.npages - 1) * PAGE_SIZE : 0;
}

uint64_t swap_free_bytes(void)
{
    return sw.active ? (uint64_t)slots_free() * PAGE_SIZE : 0;
}

static int evict_page(struct proc *p, uint64_t *pte, uint64_t va)
{
    uint64_t e = *pte;
    paddr_t pa = e & PTE_ADDR;
    uint32_t slot = slot_alloc();

    if (!slot)
        return 0;
    if (slot_io(slot, P2V(pa), 1) < 0) {
        sw.map[slot] = MAP_RESERVED;
        sw.used--;
        sw.bad++;
        if (++sw.errors >= SWAP_MAX_ERRORS) {
            sw.evict_ok = false;
            kprintf("swap: too many write errors, paging out disabled\n");
        }
        return 0;
    }
    *pte = SWAP_PTE(slot, e);
    vmm_flush_va(p->pml4, va);
    pmm_free(pa);
    return 1;
}

static bool evictable(const struct proc *p, bool self_ok)
{
    return p && p->state != P_ZOMBIE && p->pml4 != kernel_pml4 && (self_ok || p != curproc);
}

static int scan_proc(struct proc *p, unsigned pass)
{
    uint64_t cursor = p->swap_hand, *pte;

    while ((pte = vmm_next_user_pte(p->pml4, &cursor, VMM_PRESENT)) != NULL) {
        uint64_t va = cursor;

        cursor += PAGE_SIZE;
        if (!(*pte & PTE_US))
            continue;
        if (pass == 0 && (*pte & PTE_A)) {
            *pte &= ~PTE_A;
            vmm_flush_va(p->pml4, va);
            continue;
        }
        if (evict_page(p, pte, va)) {
            p->swap_hand = cursor;
            return 1;
        }
        if (!sw.evict_ok)
            break;
    }
    p->swap_hand = 0;
    return 0;
}

static int swap_evict(bool self_ok)
{
    unsigned pass, k, slot;
    int done = 0;

    if (!sw.active || !sw.evict_ok || sw.busy || slots_free() == 0)
        return 0;
    sw.busy = true;
    for (pass = 0; pass < 2 && !done && sw.evict_ok; pass++)
        for (k = 1; k < MAXPROC && !done && sw.evict_ok; k++) {
            struct proc *p;

            slot = 1 + (sw.hand_proc + k - 1) % (MAXPROC - 1);
            p = proc_slot((int)slot);
            if (!evictable(p, self_ok))
                continue;
            if (scan_proc(p, pass)) {
                sw.hand_proc = slot;
                done = 1;
            }
        }
    sw.busy = false;
    return done;
}

static int reclaim_hook(void)
{
    if (curproc && curproc->in_irq)
        return 0;
    return swap_evict(true);
}

static int swap_in_entry(uint64_t *pte, bool self_ok)
{
    uint64_t e = *pte;
    uint32_t slot = (uint32_t)PTE_SWAP_SLOT(e);
    paddr_t pa = pmm_try_alloc();

    while (!pa && swap_evict(self_ok))
        pa = pmm_try_alloc();
    if (!pa)
        return -ENOMEM;
    if (slot_io(slot, P2V(pa), 0) < 0) {
        pmm_free(pa);
        return -EIO;
    }
    *pte = pa | (e & PTE_SWAPKEEP) | PTE_A | PTE_P;
    swap_free(slot);
    return 0;
}

int swap_fault(uint64_t va)
{
    uint64_t *pte;

    if (!sw.active)
        return -EFAULT;
    pte = vmm_pte(curproc->pml4, va);
    if (!pte || !PTE_IS_SWAPPED(*pte))
        return -EFAULT;
    return swap_in_entry(pte, true);
}

static int drain_proc(struct proc *p)
{
    uint64_t cursor = 0, *pte;

    while ((pte = vmm_next_user_pte(p->pml4, &cursor, VMM_SWAPPED)) != NULL) {
        if (p->state == P_ZOMBIE) {
            swap_free(PTE_SWAP_SLOT(*pte));
            *pte = 0;
        } else {
            int r = swap_in_entry(pte, false);

            if (r < 0)
                return r;
        }
        cursor += PAGE_SIZE;
    }
    return 0;
}

int swap_on(struct blkdrv *drv, uint64_t start, uint64_t nsect, const char *name)
{
    paddr_t hp;
    uint8_t *hdr;
    uint32_t last, npages;
    uint64_t area_pages;
    int r = 0;

    if (sw.active)
        return -EBUSY;
    if (blk_overlaps_root(drv, start, nsect))
        return -EBUSY;
    if (nsect < 2 * SWAP_PAGE_SECTORS || start + nsect > drv->nsectors)
        return -EINVAL;
    hp = pmm_alloc();
    if (!hp)
        return -ENOMEM;
    hdr = P2V(hp);
    if (blk_rw_bytes(drv, start * 512, hdr, PAGE_SIZE, 0) < 0)
        r = -EIO;
    else if (memcmp(hdr + SWAP_MAGIC_OFF, SWAP_MAGIC, 10) || le32(hdr + SWAP_VERSION_OFF) != 1 ||
             le32(hdr + SWAP_BADPAGES_OFF) != 0)
        r = -EINVAL;
    last = le32(hdr + SWAP_LAST_OFF);
    pmm_free(hp);
    if (r < 0)
        return r;
    area_pages = nsect / SWAP_PAGE_SECTORS;
    if (last == 0 || last >= area_pages)
        return -EINVAL;
    npages = last + 1 > SWAP_MAX_PAGES ? SWAP_MAX_PAGES : last + 1;

    sw.map = kzalloc(npages);
    if (!sw.map)
        return -ENOMEM;
    sw.drv = drv;
    sw.start = start;
    sw.nsect = nsect;
    sw.npages = npages;
    sw.used = 0;
    sw.bad = 0;
    sw.errors = 0;
    sw.hint = 1;
    sw.hand_proc = 0;
    sw.map[0] = MAP_RESERVED;
    sw.evict_ok = true;
    sw.busy = false;
    sw.active = true;
    pmm_set_reclaim(reclaim_hook);
    kprintf("[ok] swap: %s, %lu MiB\n", name, (uint64_t)(npages - 1) * PAGE_SIZE >> 20);
    return 0;
}

int swap_off(struct blkdrv *drv, uint64_t start)
{
    unsigned i;
    int r;

    if (!sw.active)
        return -EINVAL;
    if (sw.drv != drv || sw.start != start)
        return -EINVAL;
    sw.evict_ok = false;
    for (i = 1; i < MAXPROC; i++) {
        struct proc *p = proc_slot((int)i);

        if (!p || p->pml4 == kernel_pml4)
            continue;
        r = drain_proc(p);
        if (r < 0) {
            sw.evict_ok = sw.errors < SWAP_MAX_ERRORS;
            return r;
        }
    }
    if (sw.used) {
        sw.evict_ok = sw.errors < SWAP_MAX_ERRORS;
        return -EBUSY;
    }
    pmm_set_reclaim(NULL);
    sw.active = false;
    kfree(sw.map);
    sw.map = NULL;
    sw.drv = NULL;
    sw.npages = 0;
    return 0;
}
