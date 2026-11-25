#include <proc.h>
#include <mm.h>
#include <exec.h>
#include <kern.h>
#include <io.h>
#include <sys/fcntl.h>
#include <sys/errno.h>

static struct proc *ptab[MAXPROC];
static struct proc  idle_proc;
static int          next_pid = 1;

void proc_init(void)
{
    memset(&idle_proc, 0, sizeof(idle_proc));
    idle_proc.pid   = 0;
    idle_proc.slot  = 0;
    idle_proc.state = P_RUNNABLE;
    idle_proc.pml4  = kernel_pml4;
    fpu_reset(proc_fpu(&idle_proc));
    memcpy(idle_proc.name, "idle", 5);
    ptab[0]   = &idle_proc;
    curproc   = &idle_proc;
    cpu0.ktop = 0;
}

static struct proc *find_pid(int pid)
{
    int i;

    for (i = 0; i < MAXPROC; i++)
        if (ptab[i] && ptab[i]->pid == pid)
            return ptab[i];
    return NULL;
}

int proc_count(void)
{
    int i, n = 0;

    for (i = 1; i < MAXPROC; i++)
        if (ptab[i])
            n++;
    return n;
}

static int alloc_slot(void)
{
    int slot;

    for (slot = 1; slot < MAXPROC; slot++)
        if (!ptab[slot])
            return slot;
    return -1;
}

int fd_install(struct proc *p, struct file *f)
{
    int i;

    for (i = 0; i < NOFILE; i++) {
        if (!p->fds[i]) {
            p->fds[i] = f;
            p->fdflags[i] = 0;
            return i;
        }
    }
    return -1;
}

struct file *fd_get(int fd)
{
    if (fd < 0 || fd >= NOFILE)
        return NULL;
    return curproc->fds[fd];
}

int fd_close(int fd)
{
    struct file *f = fd_get(fd);

    if (!f)
        return -1;
    curproc->fds[fd] = NULL;
    curproc->fdflags[fd] = 0;
    file_unref(f);
    return 0;
}

static void close_all_fds(struct proc *p)
{
    int i;

    for (i = 0; i < NOFILE; i++) {
        if (p->fds[i]) {
            file_unref(p->fds[i]);
            p->fds[i] = NULL;
        }
    }
}

static void set_name(struct proc *p, const char *path)
{
    const char *base = path;
    int i;

    for (i = 0; path[i]; i++)
        if (path[i] == '/')
            base = path + i + 1;
    memset(p->name, 0, sizeof(p->name));
    for (i = 0; i < 15 && base[i]; i++)
        p->name[i] = base[i];
}

static int setup_kstack(struct proc *p, const struct trapframe *tf)
{
    struct trapframe *dst;
    uint64_t *sp, top;
    int i;

    p->kstack = kmalloc(KSTACK_SIZE);
    if (!p->kstack)
        return -ENOMEM;
    top = ((uint64_t)p->kstack + KSTACK_SIZE) & ~15UL;
    p->ktop = top;

    dst = (struct trapframe *)(top - sizeof(*dst));
    *dst = *tf;

    sp = (uint64_t *)dst;
    *--sp = (uint64_t)isr_return;
    for (i = 0; i < 6; i++)
        *--sp = 0;
    p->ksp = (uint64_t)sp;
    return 0;
}

static void free_proc(struct proc *p)
{
    ptab[p->slot] = NULL;
    vmm_destroy_user(p->pml4);
    kfree(p->kstack);
    kfree(p);
}

struct proc *proc_create_exec(const char *path, char *const argv[], char *const envp[])
{
    struct proc *p;
    struct exec_image img;
    struct trapframe tf;
    int slot, i, r;

    slot = alloc_slot();
    if (slot < 0)
        return NULL;
    p = kzalloc(sizeof(*p));
    p->cmask = 022;
    if (!p)
        return NULL;

    r = exec_prepare(path, vfs_root(), argv, envp, &img);
    if (r < 0) {
        kprintf("[proc] cannot exec %s: error %d\n", path, -r);
        kfree(p);
        return NULL;
    }

    memset(&tf, 0, sizeof(tf));
    tf.rip    = img.entry;
    tf.cs     = SEL_UCODE;
    tf.ss     = SEL_UDATA;
    tf.rflags = 0x202;
    tf.rsp    = img.rsp;
    if (setup_kstack(p, &tf) < 0)
        panic("proc_create_exec: out of memory for kernel stack");

    set_name(p, path);
    p->pid  = next_pid++;
    p->ppid = p->pid == 1 ? 0 : 1;
    p->slot = slot;
    fpu_reset(proc_fpu(p));
    p->pml4 = img.pml4;
    p->brk_start = p->brk = img.brk;
    p->cwd  = vfs_root();
    for (i = 0; i < 3; i++) {
        struct file *f = file_new(&console_vnode, O_RDWR);
        if (!f || fd_install(p, f) != i)
            panic("proc_create_exec: cannot set up standard descriptors");
    }

    p->state = P_RUNNABLE;
    ptab[slot] = p;
    ktrace("[proc] pid %d (%s) created, entry %lx\n", p->pid, p->name, img.entry);
    return p;
}

long proc_fork(const struct trapframe *tf)
{
    struct proc *par = curproc, *c;
    struct trapframe ctf;
    int slot, i;

    slot = alloc_slot();
    if (slot < 0)
        return -ENOMEM;
    c = kzalloc(sizeof(*c));
    if (!c)
        return -ENOMEM;
    c->pml4 = vmm_copy_user(par->pml4);
    if (!c->pml4) {
        kfree(c);
        return -ENOMEM;
    }
    ctf = *tf;
    ctf.rax = 0;
    if (setup_kstack(c, &ctf) < 0) {
        vmm_destroy_user(c->pml4);
        kfree(c);
        return -ENOMEM;
    }
    for (i = 0; i < NOFILE; i++)
        if (par->fds[i])
            c->fds[i] = file_ref(par->fds[i]);
    fpu_save(proc_fpu(par));
    memcpy(proc_fpu(c), proc_fpu(par), FPU_AREA_SIZE);
    c->cwd = par->cwd;
    c->uid = par->uid;
    c->cmask = par->cmask;
    memcpy(c->fdflags, par->fdflags, sizeof(c->fdflags));
    memcpy(c->sig_handler, par->sig_handler, sizeof(c->sig_handler));
    c->sig_restorer = par->sig_restorer;
    c->sig_mask = par->sig_mask;
    c->pgid = par->pgid ? par->pgid : par->pid;
    c->gid = par->gid;
    c->brk_start = par->brk_start;
    c->brk = par->brk;
    memcpy(c->name, par->name, sizeof(c->name));
    c->pid = next_pid++;
    c->ppid = par->pid;
    c->slot = slot;
    c->state = P_RUNNABLE;
    ptab[slot] = c;
    ktrace("[proc] pid %d forked from pid %d\n", c->pid, par->pid);
    return c->pid;
}

void schedule(void)
{
    struct proc *old = curproc, *n = NULL;
    int i;

    for (i = 1; i <= MAXPROC; i++) {
        struct proc *p = ptab[(old->slot + i) % MAXPROC];
        if (p && p != &idle_proc && p->state == P_RUNNABLE) {
            n = p;
            break;
        }
    }
    if (!n)
        n = &idle_proc;
    if (n == old)
        return;

    curproc   = n;
    cpu0.ktop = n->ktop;
    if (n->ktop)
        tss_set_rsp0(n->ktop);
    if (n->pml4 != old->pml4)
        vmm_switch(n->pml4);
    fpu_save(proc_fpu(old));
    fpu_restore(proc_fpu(n));
    switch_context(&old->ksp, n->ksp);
}

void sched_tick(struct trapframe *tf)
{
    uint64_t now = pit_ticks();
    int i;

    for (i = 1; i < MAXPROC; i++) {
        struct proc *p = ptab[i];
        if (p && p->state == P_SLEEPING && now >= p->wake_tick)
            p->state = P_RUNNABLE;
    }
    if ((tf->cs & 3) && curproc->pending_sig)
        sig_deliver(tf);
    if (curproc == &idle_proc) {
        schedule();
    } else if ((tf->cs & 3) && ++curproc->slice >= SLICE_TICKS) {
        curproc->slice = 0;
        schedule();
    }
}

void proc_yield(void)
{
    curproc->slice = 0;
    schedule();
}

void proc_sleep_ticks(uint64_t ticks)
{
    curproc->wake_tick = pit_ticks() + (ticks ? ticks : 1);
    curproc->state = P_SLEEPING;
    schedule();
}

void proc_wait_on(void *chan)
{
    curproc->wchan = chan;
    curproc->wake_tick = ~0UL;
    curproc->state = P_SLEEPING;
    schedule();
}

void proc_wakeup(void *chan)
{
    int i;

    for (i = 1; i < MAXPROC; i++) {
        struct proc *p = ptab[i];
        if (p && p->state == P_SLEEPING && p->wchan == chan) {
            p->wchan = NULL;
            p->state = P_RUNNABLE;
        }
    }
}

int proc_owner(int pid, uint32_t *uid)
{
    struct proc *p = find_pid(pid);

    if (!p || p->pid == 0 || p->state == P_ZOMBIE)
        return -1;
    *uid = p->uid;
    return 0;
}

struct proc *proc_find(int pid)
{
    return find_pid(pid);
}

struct proc *proc_slot(int slot)
{
    return slot > 0 && slot < MAXPROC ? ptab[slot] : NULL;
}

int proc_signal_pgrp(int pgid, int sig)
{
    int i, n = 0;

    for (i = 1; i < MAXPROC; i++) {
        struct proc *p = ptab[i];

        if (p && p->state != P_ZOMBIE && (p->pgid ? p->pgid : p->pid) == pgid) {
            p->pending_sig = sig;
            if (p->state == P_SLEEPING) {
                p->wchan = NULL;
                p->state = P_RUNNABLE;
            }
            n++;
        }
    }
    return n ? 0 : -1;
}

int proc_signal(int pid, int sig)
{
    struct proc *p = find_pid(pid);

    if (!p || p->pid == 0 || p->state == P_ZOMBIE)
        return -1;
    p->pending_sig = sig;
    if (p->state == P_SLEEPING) {
        p->wchan = NULL;
        p->state = P_RUNNABLE;
    }
    return 0;
}

static __noreturn void exit_common(int wstatus)
{
    struct proc *p = curproc, *parent;
    bool wake_init = false;
    int i;

    if (p->pid == 1)
        panic("init exited, wait status %x", wstatus);
    ktrace("[proc] pid %d (%s) exited, wait status %x\n", p->pid, p->name, wstatus);
    close_all_fds(p);

    for (i = 1; i < MAXPROC; i++) {
        struct proc *q = ptab[i];
        if (q && q->ppid == p->pid) {
            q->ppid = 1;
            if (q->state == P_ZOMBIE)
                wake_init = true;
        }
    }
    p->wstatus = wstatus;
    p->state = P_ZOMBIE;

    parent = find_pid(p->ppid);
    if (parent)
        proc_wakeup(parent);
    if (wake_init && (parent = find_pid(1)) != NULL)
        proc_wakeup(parent);
    schedule();
    panic("zombie was scheduled again");
}

void proc_exit(int code)
{
    exit_common((code & 0xFF) << 8);
}

void proc_kill_self(int sig)
{
    exit_common(sig & 0x7F);
}

long proc_wait(int pid, int options, int *wstatus)
{
    struct proc *me = curproc;
    int i;

    for (;;) {
        bool has_child = false;

        for (i = 1; i < MAXPROC; i++) {
            struct proc *p = ptab[i];

            if (!p || p->ppid != me->pid)
                continue;
            if (pid > 0 && p->pid != pid)
                continue;
            has_child = true;
            if (p->state == P_ZOMBIE) {
                int cpid = p->pid;

                *wstatus = p->wstatus;
                free_proc(p);
                return cpid;
            }
        }
        if (!has_child)
            return -ECHILD;
        if (options & WNOHANG)
            return 0;
        if (me->pending_sig)
            return -EINTR;
        proc_wait_on(me);
    }
}
