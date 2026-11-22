#ifndef _LIBC_SYS_WAIT_H
#define _LIBC_SYS_WAIT_H

#include <sys/types.h>

#define WNOHANG 1

#define WIFEXITED(s)   (((s) & 0x7F) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)
#define WIFSIGNALED(s) (((s) & 0x7F) != 0)
#define WTERMSIG(s)    ((s) & 0x7F)

pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait(int *status);

#define WUNTRACED 2
#define WIFSTOPPED(s) (((s) & 0xFF) == 0x7F)
#define WSTOPSIG(s) (((s) >> 8) & 0xFF)

pid_t wait3(int *status, int options, void *usage);

#endif
