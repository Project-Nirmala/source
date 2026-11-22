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

/*
 * Minimal vsscanf: %d %i %u %x %ld %lu %lx %s %c %% and whitespace.
 * Enough for curses scanw() usage; no floating point, no %[ sets.
 */
int vsscanf(const char *str, const char *fmt, va_list ap)
{
    int count = 0;
    const char *s = str;

    while (*fmt) {
        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*s))
                s++;
            fmt++;
        } else if (*fmt != '%') {
            if (*s != *fmt)
                return count;
            s++;
            fmt++;
        } else {
            int lng = 0, width = 0;
            char *end;

            fmt++;
            if (*fmt == '%') {
                if (*s != '%')
                    return count;
                s++;
                fmt++;
                continue;
            }
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
            while (*fmt == 'l') {
                lng++;
                fmt++;
            }
            switch (*fmt) {
            case 'd': case 'i': case 'u': case 'x': {
                long v;
                int base = (*fmt == 'x') ? 16 : 10;

                while (isspace((unsigned char)*s))
                    s++;
                if (!*s)
                    return count ? count : -1;
                if (*fmt == 'u')
                    v = (long)strtoul(s, &end, base);
                else
                    v = strtol(s, &end, base);
                if (end == s)
                    return count;
                s = end;
                if (lng)
                    *va_arg(ap, long *) = v;
                else
                    *va_arg(ap, int *) = (int)v;
                count++;
                break;
            }
            case 's': {
                char *d = va_arg(ap, char *);

                while (isspace((unsigned char)*s))
                    s++;
                if (!*s)
                    return count ? count : -1;
                while (*s && !isspace((unsigned char)*s) &&
                       (!width || width-- > 0))
                    *d++ = *s++;
                *d = '\0';
                count++;
                break;
            }
            case 'c': {
                char *d = va_arg(ap, char *);

                if (!*s)
                    return count ? count : -1;
                *d = *s++;
                count++;
                break;
            }
            default:
                return count;
            }
            fmt++;
        }
    }
    return count;
}
