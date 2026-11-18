#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include "nirmake.h"

const char *nm_progname = "nirmake";
const char *nm_file = NULL;
int nm_line = 0;
int nm_errors = 0;

void *nm_malloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (p == NULL) {
        fprintf(stderr, "%s: out of memory\n", nm_progname);
        exit(2);
    }
    return p;
}

void *nm_realloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (q == NULL) {
        fprintf(stderr, "%s: out of memory\n", nm_progname);
        exit(2);
    }
    return q;
}

char *nm_strndup(const char *s, size_t n)
{
    char *r = (char *)nm_malloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

char *nm_strdup(const char *s)
{
    return nm_strndup(s, strlen(s));
}

static void vmsg(const char *kind, const char *fmt, va_list ap)
{
    char msg[4096];
    fflush(stdout);
    vsprintf(msg, fmt, ap);
    if (nm_file != NULL && nm_line > 0)
        fprintf(stderr, "%s: \"%s\" line %d: %s%s\n", nm_progname, nm_file, nm_line, kind, msg);
    else
        fprintf(stderr, "%s: %s%s\n", nm_progname, kind, msg);
}

void nm_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmsg("", fmt, ap);
    va_end(ap);
    exit(2);
}

void nm_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmsg("", fmt, ap);
    va_end(ap);
    nm_errors++;
}

void nm_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmsg("warning: ", fmt, ap);
    va_end(ap);
}

void nm_info(const char *fmt, ...)
{
    va_list ap;
    char msg[4096];
    va_start(ap, fmt);
    vsprintf(msg, fmt, ap);
    va_end(ap);
    fflush(stdout);
    if (nm_file != NULL && nm_line > 0)
        fprintf(stderr, "%s: \"%s\" line %d: %s\n", nm_progname, nm_file, nm_line, msg);
    else
        fprintf(stderr, "%s: %s\n", nm_progname, msg);
}

void buf_init(Buf *b)
{
    b->cap = 64;
    b->len = 0;
    b->p = (char *)nm_malloc(b->cap);
    b->p[0] = '\0';
}

static void buf_grow(Buf *b, size_t need)
{
    if (b->len + need + 1 > b->cap) {
        while (b->len + need + 1 > b->cap)
            b->cap *= 2;
        b->p = (char *)nm_realloc(b->p, b->cap);
    }
}

void buf_addc(Buf *b, int c)
{
    buf_grow(b, 1);
    b->p[b->len++] = (char)c;
    b->p[b->len] = '\0';
}

void buf_addn(Buf *b, const char *s, size_t n)
{
    buf_grow(b, n);
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

void buf_adds(Buf *b, const char *s)
{
    buf_addn(b, s, strlen(s));
}

char *buf_take(Buf *b)
{
    char *r = b->p;
    b->p = NULL;
    b->len = b->cap = 0;
    return r;
}

void buf_free(Buf *b)
{
    free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

void sl_init(StrList *l)
{
    l->v = NULL;
    l->n = 0;
    l->cap = 0;
}

void sl_addown(StrList *l, char *s)
{
    if (l->n + 1 > l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = (char **)nm_realloc(l->v, (size_t)l->cap * sizeof(char *));
    }
    l->v[l->n++] = s;
}

void sl_add(StrList *l, const char *s)
{
    sl_addown(l, nm_strdup(s));
}

void sl_free(StrList *l)
{
    int i;
    for (i = 0; i < l->n; i++)
        free(l->v[i]);
    free(l->v);
    sl_init(l);
}

int sl_has(const StrList *l, const char *s)
{
    int i;
    for (i = 0; i < l->n; i++)
        if (strcmp(l->v[i], s) == 0)
            return 1;
    return 0;
}

char *sl_join(const StrList *l, const char *sep)
{
    Buf b;
    int i;
    buf_init(&b);
    for (i = 0; i < l->n; i++) {
        if (i > 0)
            buf_adds(&b, sep);
        buf_adds(&b, l->v[i]);
    }
    return buf_take(&b);
}

const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

char *str_trim(char *s)
{
    size_t n;
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n'))
        s[--n] = '\0';
    return s;
}

void split_words(const char *s, StrList *out)
{
    const char *p = s;
    while (*p) {
        const char *start;
        int quote = 0;
        Buf b;
        while (isspace((unsigned char)*p))
            p++;
        if (*p == '\0')
            break;
        start = p;
        buf_init(&b);
        while (*p) {
            if (quote) {
                if (*p == '\\' && p[1] != '\0') {
                    buf_addc(&b, *p++);
                } else if (*p == quote) {
                    quote = 0;
                }
                buf_addc(&b, *p++);
                continue;
            }
            if (isspace((unsigned char)*p))
                break;
            if (*p == '\'' || *p == '"')
                quote = *p;
            else if (*p == '\\' && p[1] != '\0')
                buf_addc(&b, *p++);
            buf_addc(&b, *p++);
        }
        (void)start;
        sl_addown(out, buf_take(&b));
    }
}

char *nm_dirname(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL)
        return nm_strdup(".");
    if (slash == path)
        return nm_strdup("/");
    return nm_strndup(path, (size_t)(slash - path));
}

const char *nm_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

char *run_capture(const char *cmd, int *status)
{
    FILE *fp;
    Buf b;
    char chunk[512];
    size_t n;
    int st;
    char *r;
    size_t len;
    size_t i;

    fflush(stdout);
    fflush(stderr);
    fp = popen(cmd, "r");
    if (fp == NULL) {
        if (status)
            *status = -1;
        return nm_strdup("");
    }
    buf_init(&b);
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0)
        buf_addn(&b, chunk, n);
    st = pclose(fp);
    if (status)
        *status = st;
    r = buf_take(&b);
    len = strlen(r);
    while (len > 0 && r[len - 1] == '\n')
        r[--len] = '\0';
    for (i = 0; i < len; i++)
        if (r[i] == '\n')
            r[i] = ' ';
    return r;
}
