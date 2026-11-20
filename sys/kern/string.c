#include <kern.h>

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    uint64_t q = (uint64_t)(unsigned char)c * 0x0101010101010101UL;

    while (n >= 32) {
        __builtin_memcpy(p, &q, 8);
        __builtin_memcpy(p + 8, &q, 8);
        __builtin_memcpy(p + 16, &q, 8);
        __builtin_memcpy(p + 24, &q, 8);
        p += 32;
        n -= 32;
    }
    while (n >= 8) {
        __builtin_memcpy(p, &q, 8);
        p += 8;
        n -= 8;
    }
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;

    while (n >= 32) {
        uint64_t a, b, c, e;

        __builtin_memcpy(&a, sp, 8);
        __builtin_memcpy(&b, sp + 8, 8);
        __builtin_memcpy(&c, sp + 16, 8);
        __builtin_memcpy(&e, sp + 24, 8);
        __builtin_memcpy(dp, &a, 8);
        __builtin_memcpy(dp + 8, &b, 8);
        __builtin_memcpy(dp + 16, &c, 8);
        __builtin_memcpy(dp + 24, &e, 8);
        dp += 32;
        sp += 32;
        n -= 32;
    }
    while (n >= 8) {
        uint64_t t;

        __builtin_memcpy(&t, sp, 8);
        __builtin_memcpy(dp, &t, 8);
        dp += 8;
        sp += 8;
        n -= 8;
    }
    while (n--)
        *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;

    if (dp <= sp || dp >= sp + n)
        return memcpy(d, s, n);
    dp += n;
    sp += n;
    while (n >= 8) {
        uint64_t t;

        dp -= 8;
        sp -= 8;
        n -= 8;
        __builtin_memcpy(&t, sp, 8);
        __builtin_memcpy(dp, &t, 8);
    }
    while (n--)
        *--dp = *--sp;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--) {
        if (*x != *y)
            return *x - *y;
        x++;
        y++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
