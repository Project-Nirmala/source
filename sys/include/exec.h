#ifndef _NIRMALA_EXEC_H
#define _NIRMALA_EXEC_H

#include <types.h>
#include <vfs.h>
#include <cpu.h>

#define EXEC_MAX_ARGS    64
#define EXEC_MAX_ARGLEN  1024
#define EXEC_MAX_STRBYTES 8192

struct proc;

struct exec_image {
    uint64_t pml4;
    uint64_t entry;
    uint64_t rsp;
    uint64_t brk;
};

int  exec_prepare(const char *path, struct vnode *cwd, char *const argv[],
                  char *const envp[], struct exec_image *img);
void exec_commit(struct proc *p, const struct exec_image *img, struct trapframe *tf);

#endif
