#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "nc.h"

void nc_die(const char *fmt, ...)
{
    va_list ap;

    fputs("nirconfig: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void nc_warn(const char *fmt, ...)
{
    va_list ap;

    fputs("nirconfig: warning: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void *nc_xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);

    if (!p)
        nc_die("out of memory");
    return p;
}

void *nc_xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);

    if (!q)
        nc_die("out of memory");
    return q;
}

char *nc_xstrndup(const char *s, size_t n)
{
    char *p = nc_xmalloc(n + 1);

    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *nc_xstrdup(const char *s)
{
    return nc_xstrndup(s, strlen(s));
}

void nc_buf_init(struct nc_buf *b)
{
    b->cap = 64;
    b->len = 0;
    b->p = nc_xmalloc(b->cap);
    b->p[0] = '\0';
}

void nc_buf_addn(struct nc_buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap)
            b->cap *= 2;
        b->p = nc_xrealloc(b->p, b->cap);
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

void nc_buf_add(struct nc_buf *b, const char *s)
{
    nc_buf_addn(b, s, strlen(s));
}

void nc_buf_addc(struct nc_buf *b, int c)
{
    char ch = (char)c;

    nc_buf_addn(b, &ch, 1);
}

char *nc_buf_take(struct nc_buf *b)
{
    char *p = b->p;

    b->p = NULL;
    b->len = 0;
    b->cap = 0;
    return p;
}

char *nc_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    struct nc_buf b;
    char tmp[4096];
    size_t n;

    if (!f)
        return NULL;
    nc_buf_init(&b);
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0)
        nc_buf_addn(&b, tmp, n);
    fclose(f);
    if (len)
        *len = b.len;
    return nc_buf_take(&b);
}
