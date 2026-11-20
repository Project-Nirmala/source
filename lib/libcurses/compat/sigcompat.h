#ifndef _COMPAT_SIGCOMPAT_H
#define _COMPAT_SIGCOMPAT_H
#include <signal.h>
#ifndef SIGWINCH
#define SIGWINCH 28
#endif
#ifndef SIGTSTP
#define SIGTSTP 20
#endif
typedef unsigned long sigset_t;
struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    int sa_flags;
};
#define SIG_BLOCK 0
#define SIG_SETMASK 2
int sigaction(int sig, const struct sigaction *act, struct sigaction *old);
int sigemptyset(sigset_t *s);
int sigaddset(sigset_t *s, int sig);
int sigprocmask(int how, const sigset_t *set, sigset_t *old);
#endif
