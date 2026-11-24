#ifndef _LIBC_SYS_RESOURCE_H
#define _LIBC_SYS_RESOURCE_H

typedef unsigned long rlim_t;
struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

int getrlimit(int res, struct rlimit *rl);
int setrlimit(int res, const struct rlimit *rl);

#define RLIM_INFINITY (~0UL)
#define RLIMIT_FSIZE 1

#define RLIMIT_CPU 0
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_NOFILE 7
#define RLIMIT_AS 9

#ifndef PRIO_PROCESS
#define PRIO_PROCESS 0
#define PRIO_PGRP 1
#define PRIO_USER 2
#endif

int getpriority(int which, int who);
int setpriority(int which, int who, int prio);

#endif
