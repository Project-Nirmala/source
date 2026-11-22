#ifndef _LIBC_SYS_TIMES_H
#define _LIBC_SYS_TIMES_H

typedef long clock_t;

struct tms {
    clock_t tms_utime;
    clock_t tms_stime;
    clock_t tms_cutime;
    clock_t tms_cstime;
};

clock_t times(struct tms *buf);
long sysconf(int name);
#define _SC_CLK_TCK 2

#endif
