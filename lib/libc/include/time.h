#ifndef _LIBC_TIME_H
#define _LIBC_TIME_H

#include <stddef.h>
#include <sys/types.h>

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

struct timeval {
    time_t tv_sec;
    long   tv_usec;
};

struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};

#define UTIME_NOW  ((1L << 30) - 1L)
#define UTIME_OMIT ((1L << 30) - 2L)

char *strptime(const char *s, const char *fmt, struct tm *tm);

time_t     time(time_t *t);
int        clock_settime(int clock, const struct timespec *ts);
int        clock_gettime(int clock, struct timespec *ts);
int        gettimeofday(struct timeval *tv, void *tz);
struct tm *gmtime(const time_t *t);
struct tm *gmtime_r(const time_t *t, struct tm *out);
struct tm *localtime(const time_t *t);
struct tm *localtime_r(const time_t *t, struct tm *out);
time_t     timegm(struct tm *tm);

size_t     strftime(char *s, size_t max, const char *fmt, const struct tm *tm);
time_t     mktime(struct tm *tm);

#endif
