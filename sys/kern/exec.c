#include <exec.h>
#include <elf.h>
#include <proc.h>
#include <mm.h>
#include <kern.h>
#include <sys/errno.h>

static int user_write(uint64_t pml4, uint64_t va, const void *src, size_t len)
{
    const uint8_t *s = src;

    while (len) {
        uint64_t pa;
        size_t chunk = PAGE_SIZE - (va & (PAGE_SIZE - 1));

        if (chunk > len)
            chunk = len;
        if (!vmm_translate(pml4, va, &pa, NULL))
            return -EFAULT;
        memcpy(P2V(pa), s, chunk);
        va += chunk;
        s += chunk;
        len -= chunk;
    }
    return 0;
}

static int stack_setup(uint64_t pml4, char *const argv[], char *const envp[], uint64_t *rsp_out)
{
    uint64_t sp = USER_STACK_TOP, *ptrs, *frame;
    size_t strbytes = 0, words;
    int argc = 0, envc = 0, i, r = 0;

    for (i = 0; i < USER_STACK_PAGES; i++) {
        paddr_t pa = pmm_alloc_zero();
        if (!pa || vmm_map(pml4, USER_STACK_TOP - (uint64_t)(i + 1) * PAGE_SIZE, pa,
                           PTE_RW | PTE_US | PTE_NX) < 0)
            return -ENOMEM;
    }

    while (argv && argv[argc])
        strbytes += strlen(argv[argc++]) + 1;
    while (envp && envp[envc])
        strbytes += strlen(envp[envc++]) + 1;
    if (argc > EXEC_MAX_ARGS || envc > EXEC_MAX_ARGS || strbytes > EXEC_MAX_STRBYTES)
        return -E2BIG;

    ptrs = kmalloc((size_t)(argc + envc + 2) * sizeof(uint64_t));
    if (!ptrs)
        return -ENOMEM;

    for (i = envc - 1; i >= 0 && r == 0; i--) {
        size_t len = strlen(envp[i]) + 1;
        sp -= len;
        ptrs[argc + 1 + i] = sp;
        r = user_write(pml4, sp, envp[i], len);
    }
    for (i = argc - 1; i >= 0 && r == 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        sp -= len;
        ptrs[i] = sp;
        r = user_write(pml4, sp, argv[i], len);
    }
    if (r < 0) {
        kfree(ptrs);
        return r;
    }

    sp &= ~15UL;
    words = 1 + (size_t)(argc + 1) + (size_t)(envc + 1) + 2;
    if (words & 1)
        sp -= 8;
    sp -= words * 8;

    frame = kzalloc(words * sizeof(uint64_t));
    if (!frame) {
        kfree(ptrs);
        return -ENOMEM;
    }
    frame[0] = (uint64_t)argc;
    for (i = 0; i < argc; i++)
        frame[1 + i] = ptrs[i];
    for (i = 0; i < envc; i++)
        frame[1 + argc + 1 + i] = ptrs[argc + 1 + i];

    r = user_write(pml4, sp, frame, words * sizeof(uint64_t));
    kfree(frame);
    kfree(ptrs);
    if (r < 0)
        return r;
    *rsp_out = sp;
    return 0;
}

int exec_prepare(const char *path, struct vnode *cwd, char *const argv[],
                 char *const envp[], struct exec_image *img)
{
    struct vnode *vn;
    uint64_t pml4;
    int r;

    r = vfs_lookup(path, cwd, &vn);
    if (r < 0)
        return r;
    if (vn->type != VREG || vfs_perm(vn, VP_X) < 0)
        return -EACCES;
    pml4 = vmm_new_user();
    if (!pml4)
        return -ENOMEM;
    r = elf_load(vn, pml4, &img->entry, &img->brk);
    if (r == 0)
        r = stack_setup(pml4, argv, envp, &img->rsp);
    if (r < 0) {
        vmm_destroy_user(pml4);
        return r;
    }
    img->pml4 = pml4;
    return 0;
}

void exec_commit(struct proc *p, const struct exec_image *img, struct trapframe *tf)
{
    uint64_t old = p->pml4;

    p->pml4 = img->pml4;
    p->brk_start = p->brk = img->brk;
    for (int i = 0; i < 32; i++)
        if (p->sig_handler[i] > 1)
            p->sig_handler[i] = 0;
    p->sig_restorer = 0;
    p->sig_mask = 0;
    fpu_reset(proc_fpu(p));
    fpu_restore(proc_fpu(p));
    vmm_switch(img->pml4);
    if (old != kernel_pml4)
        vmm_destroy_user(old);
    memset(tf, 0, sizeof(*tf));
    tf->rip = img->entry;
    tf->cs = SEL_UCODE;
    tf->ss = SEL_UDATA;
    tf->rflags = 0x202;
    tf->rsp = img->rsp;
}
