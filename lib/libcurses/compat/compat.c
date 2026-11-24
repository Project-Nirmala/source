#include <signal.h>
#include "sigcompat.h"

#include <stdio.h>
#include <stdlib.h>
#include <curses.h>

int wcwidth(int c)
{
    return c >= 0x20 && c < 0x7F ? 1 : (c == 0 ? 0 : -1);
}

int wctomb(char *s, int wc)
{
    if (s)
        *s = (char)wc;
    return 1;
}

unsigned btowc(int c)
{
    return c == EOF ? (unsigned)-1 : (unsigned)c;
}

FILE *open_memstream(char **buf, size_t *size)
{
    (void)buf;
    (void)size;
    return NULL;
}

int fpurge(FILE *f)
{
    (void)f;
    return 0;
}

int wattr_on(WINDOW *w, attr_t a, void *opts)
{
    (void)opts;
    return wattron(w, (int)a);
}

int wattr_off(WINDOW *w, attr_t a, void *opts)
{
    (void)opts;
    return wattroff(w, (int)a);
}

#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    unsigned long long us;

    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    if (!req)
        return -1;
    us = (unsigned long long)req->tv_sec * 1000000ULL +
         (unsigned long long)(req->tv_nsec / 1000);
    while (us > 1000000ULL) {
        usleep(1000000u);
        us -= 1000000ULL;
    }
    usleep((unsigned)us);
    return 0;
}

int vasprintf(char **out, const char *fmt, va_list ap)
{
    va_list ap2;
    int n;
    char *p;

    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0)
        return -1;
    p = malloc((size_t)n + 1);
    if (!p)
        return -1;
    vsnprintf(p, (size_t)n + 1, fmt, ap);
    *out = p;
    return n;
}

int asprintf(char **out, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vasprintf(out, fmt, ap);
    va_end(ap);
    return n;
}

