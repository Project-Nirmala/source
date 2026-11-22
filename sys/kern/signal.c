#include <stdint.h>
#include <stddef.h>
#include "proc.h"
#include "cpu.h"
#include "uaccess.h"
#include "errno.h"

#define SIGKILL_N 9
#define SIGSTOP_N 19
#define USER_TOP  0x0000800000000000UL

struct ksigaction {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask;
};

struct sigframe {
    uint64_t ret;
    struct trapframe tf;
    uint64_t mask;
};

int sig_deliver(struct trapframe *tf)
{
    struct proc *p = curproc;
    struct sigframe f;
    uint64_t base;
    int sig = p->pending_sig;
    uint64_t h;

    if (!sig)
        return 0;
    if (sig != SIGKILL_N && sig != SIGSTOP_N && (p->sig_mask >> sig) & 1)
        return 0;
    h = p->sig_handler[sig];
    if (sig == SIGKILL_N || h == 0)
        proc_kill_self(sig);
    p->pending_sig = 0;
    if (h == 1 || !p->sig_restorer)
        return 0;
    f.ret = p->sig_restorer;
    f.tf = *tf;
    f.mask = p->sig_mask;
    base = ((tf->rsp - 128 - sizeof(f)) & ~15UL) - 8;
    if (copy_to_user(base, &f, sizeof(f)) < 0)
        proc_kill_self(11);
    p->sig_mask |= 1UL << sig;
    tf->rsp = base;
    tf->rip = h;
    tf->rdi = (uint64_t)sig;
    return 1;
}

long sys_sigaction(uint64_t sig, uint64_t act, uint64_t old, uint64_t size)
{
    struct proc *p = curproc;
    struct ksigaction k;

    if (sig < 1 || sig > 31 || sig == SIGKILL_N || sig == SIGSTOP_N || size != 8)
        return -EINVAL;
    if (old) {
        k.handler = p->sig_handler[sig];
        k.flags = 0;
        k.restorer = p->sig_restorer;
        k.mask = 0;
        if (copy_to_user(old, &k, sizeof(k)) < 0)
            return -EFAULT;
    }
    if (act) {
        if (copy_from_user(&k, act, sizeof(k)) < 0)
            return -EFAULT;
        if (k.handler > 1 && k.restorer)
            p->sig_restorer = k.restorer;
        p->sig_handler[sig] = k.handler;
    }
    return 0;
}

long sys_sigprocmask(uint64_t how, uint64_t set, uint64_t old, uint64_t size)
{
    struct proc *p = curproc;
    uint64_t m, prev = p->sig_mask;

    if (size != 8)
        return -EINVAL;
    if (old && copy_to_user(old, &prev, sizeof(prev)) < 0)
        return -EFAULT;
    if (!set)
        return 0;
    if (copy_from_user(&m, set, sizeof(m)) < 0)
        return -EFAULT;
    m &= ~((1UL << SIGKILL_N) | (1UL << SIGSTOP_N) | 1UL);
    if (how == 0)
        p->sig_mask |= m;
    else if (how == 1)
        p->sig_mask &= ~m;
    else if (how == 2)
        p->sig_mask = m;
    else
        return -EINVAL;
    return 0;
}

long sys_sigreturn(struct trapframe *tf)
{
    struct proc *p = curproc;
    struct sigframe f;

    if (copy_from_user(&f, tf->rsp - 8, sizeof(f)) < 0)
        proc_kill_self(11);
    if (f.tf.rip >= USER_TOP || f.tf.rsp >= USER_TOP)
        proc_kill_self(11);
    f.tf.cs = tf->cs;
    f.tf.ss = tf->ss;
    f.tf.rflags = (f.tf.rflags & 0x8D5UL) | 0x202UL;
    *tf = f.tf;
    p->sig_mask = f.mask & ~((1UL << SIGKILL_N) | (1UL << SIGSTOP_N));
    return (long)f.tf.rax;
}

long sys_setpgid(int64_t pid, int64_t pgid)
{
    struct proc *p = curproc, *t;

    if (pid < 0 || pgid < 0)
        return -EINVAL;
    t = pid ? proc_find((int)pid) : p;
    if (!t || (t != p && t->ppid != p->pid))
        return -ESRCH;
    t->pgid = pgid ? (int)pgid : t->pid;
    return 0;
}

long sys_getpgid(int64_t pid)
{
    struct proc *t = pid ? proc_find((int)pid) : curproc;

    if (!t)
        return -ESRCH;
    return t->pgid ? t->pgid : t->pid;
}
