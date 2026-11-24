#ifndef _NIRMALA_PROC_H
#define _NIRMALA_PROC_H

#include <types.h>
#include <cpu.h>
#include <file.h>
#include <fpu.h>

#define KSTACK_SIZE 16384
#define MAXPROC     64
#define SLICE_TICKS 5
#define WNOHANG     1

enum pstate { P_UNUSED, P_RUNNABLE, P_SLEEPING, P_ZOMBIE };

struct proc {
    int         pid;
    int         slot;
    enum pstate state;
    uint64_t    ksp;
    uint8_t    *kstack;
    uint64_t    ktop;
    uint64_t    pml4;
    uint64_t    brk_start, brk;
    uint64_t    wake_tick;
    int         slice;
    int         ppid;
    int         wstatus;
    char        name[16];
    void       *wchan;
    int         pending_sig;
    uint64_t    sig_handler[32];
    uint64_t    sig_restorer;
    uint64_t    sig_mask;
    int         pgid;
    uint32_t    cmask;
    uint8_t     fdflags[NOFILE];
    uint32_t    uid, gid;
    struct file *fds[NOFILE];
    struct vnode *cwd;
    uint8_t     fpu_raw[FPU_AREA_SIZE + 16];
};

struct cpu {
    struct cpu  *self;
    uint64_t     ktop;
    uint64_t     scratch;
    struct proc *cur;
};

extern struct cpu cpu0;
#define curproc (cpu0.cur)

void cpu_init(void);
void proc_init(void);
struct proc *proc_create_exec(const char *path, char *const argv[], char *const envp[]);
int fd_install(struct proc *p, struct file *f);
struct file *fd_get(int fd);
int fd_close(int fd);
void proc_exit(int code) __noreturn;
void proc_kill_self(int sig) __noreturn;
long proc_fork(const struct trapframe *tf);
struct proc *proc_find(int pid);
int proc_signal_pgrp(int pgid, int sig);
int sig_deliver(struct trapframe *tf);
long sys_sigaction(uint64_t sig, uint64_t act, uint64_t old, uint64_t size);
long sys_sigprocmask(uint64_t how, uint64_t set, uint64_t old, uint64_t size);
long sys_sigreturn(struct trapframe *tf);
long sys_setpgid(int64_t pid, int64_t pgid);
long sys_getpgid(int64_t pid);
long proc_brk(struct proc *p, uint64_t addr);
long proc_wait(int pid, int options, int *wstatus);
void proc_sleep_ticks(uint64_t ticks);
void proc_wait_on(void *chan);
void proc_wakeup(void *chan);
int  proc_signal(int pid, int sig);
int  proc_owner(int pid, uint32_t *uid);
int  proc_count(void);
static inline void *proc_fpu(struct proc *p) { return fpu_align(p->fpu_raw); }
void proc_yield(void);
void schedule(void);
void sched_tick(struct trapframe *tf);

void switch_context(uint64_t *old_ksp, uint64_t new_ksp);
extern void isr_return(void);
extern void syscall_entry(void);
void syscall_dispatch(struct trapframe *tf);

#endif
