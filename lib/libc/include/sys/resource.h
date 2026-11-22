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

#endif
