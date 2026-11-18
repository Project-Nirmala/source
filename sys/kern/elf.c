#include <elf.h>
#include <mm.h>
#include <kern.h>
#include <sys/errno.h>

#define SEGMENT_LIMIT (USER_STACK_TOP - 0x1000000UL)

static int load_segment(struct vnode *vn, uint64_t pml4, const struct elf64_phdr *ph)
{
    uint64_t start = PAGE_ALIGN_DOWN(ph->p_vaddr);
    uint64_t end = PAGE_ALIGN_UP(ph->p_vaddr + ph->p_memsz);
    uint64_t flags = PTE_US, va, off;

    if (ph->p_memsz == 0)
        return 0;
    if (ph->p_vaddr < 0x10000 || ph->p_vaddr + ph->p_memsz < ph->p_vaddr ||
        end > SEGMENT_LIMIT || ph->p_filesz > ph->p_memsz ||
        ph->p_offset + ph->p_filesz < ph->p_offset ||
        ph->p_offset + ph->p_filesz > vn->size)
        return -ENOEXEC;
    if (ph->p_flags & PF_W)
        flags |= PTE_RW;
    if (!(ph->p_flags & PF_X))
        flags |= PTE_NX;

    for (va = start; va < end; va += PAGE_SIZE) {
        paddr_t pa;

        if (vmm_translate(pml4, va, NULL, NULL))
            return -ENOEXEC;
        pa = pmm_alloc_zero();
        if (!pa)
            return -ENOMEM;
        if (vmm_map(pml4, va, pa, flags) < 0) {
            pmm_free(pa);
            return -ENOMEM;
        }
    }

    for (off = 0; off < ph->p_filesz;) {
        uint64_t a = ph->p_vaddr + off, pa;
        size_t chunk = PAGE_SIZE - (a & (PAGE_SIZE - 1));
        long n;

        if (chunk > ph->p_filesz - off)
            chunk = ph->p_filesz - off;
        if (!vmm_translate(pml4, a, &pa, NULL))
            return -ENOEXEC;
        n = vnode_read(vn, ph->p_offset + off, P2V(pa), chunk);
        if (n != (long)chunk)
            return -ENOEXEC;
        off += chunk;
    }
    return 0;
}

int elf_load(struct vnode *vn, uint64_t pml4, uint64_t *entry, uint64_t *brk)
{
    uint64_t top = 0;
    struct elf64_ehdr eh;
    struct elf64_phdr *ph;
    size_t bytes;
    int i, r = 0;

    if (vnode_read(vn, 0, &eh, sizeof(eh)) != (long)sizeof(eh))
        return -ENOEXEC;
    if (memcmp(eh.e_ident, "\177ELF", 4) || eh.e_ident[4] != 2 || eh.e_ident[5] != 1 ||
        eh.e_type != ET_EXEC || eh.e_machine != EM_X86_64 ||
        eh.e_phentsize != sizeof(struct elf64_phdr) || eh.e_phnum == 0 || eh.e_phnum > 16)
        return -ENOEXEC;
    if (eh.e_entry < 0x10000 || eh.e_entry >= SEGMENT_LIMIT)
        return -ENOEXEC;

    bytes = (size_t)eh.e_phnum * sizeof(*ph);
    ph = kmalloc(bytes);
    if (!ph)
        return -ENOMEM;
    if (vnode_read(vn, eh.e_phoff, ph, bytes) != (long)bytes) {
        kfree(ph);
        return -ENOEXEC;
    }
    for (i = 0; i < eh.e_phnum && r == 0; i++)
        if (ph[i].p_type == PT_LOAD) {
            r = load_segment(vn, pml4, &ph[i]);
            if (PAGE_ALIGN_UP(ph[i].p_vaddr + ph[i].p_memsz) > top)
                top = PAGE_ALIGN_UP(ph[i].p_vaddr + ph[i].p_memsz);
        }
    kfree(ph);
    if (r == 0) {
        *entry = eh.e_entry;
        *brk = top;
    }
    return r;
}
