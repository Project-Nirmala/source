#ifndef _NIRMALA_KERN_H
#define _NIRMALA_KERN_H

#include <types.h>

void kputs(const char *s);
void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list ap);

__noreturn void panic(const char *fmt, ...);
#ifdef CONFIG_TRACE
#define ktrace(...) kprintf(__VA_ARGS__)
#else
#define ktrace(...) ((void)0)
#endif

#define KASSERT(c) \
    do { if (!(c)) panic("assertion failed: %s (%s:%d)", #c, __FILE__, __LINE__); } while (0)

void  *memset(void *d, int c, size_t n);
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);

#endif
