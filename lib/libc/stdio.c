#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>

#define F_READ   0x01
#define F_WRITE  0x02
#define F_EOF    0x04
#define F_ERR    0x08
#define F_UNBUF  0x10
#define F_STATIC 0x20

struct _FILE {
    int            fd;
    int            flags;
    unsigned char *buf;
    size_t         bufsize;
    size_t         rpos, rlen;
    size_t         wlen;
    int            ungot;
    pid_t          pid;
    struct _FILE  *next;
};

static FILE f_stdin  = { 0, F_READ | F_STATIC, 0, 0, 0, 0, 0, -1, 0, 0 };
static FILE f_stdout = { 1, F_WRITE | F_UNBUF | F_STATIC, 0, 0, 0, 0, 0, -1, 0, 0 };
static FILE f_stderr = { 2, F_WRITE | F_UNBUF | F_STATIC, 0, 0, 0, 0, 0, -1, 0, 0 };
FILE *stdin  = &f_stdin;
FILE *stdout = &f_stdout;
FILE *stderr = &f_stderr;
static FILE *open_files;

static int write_all(int fd, const void *p, size_t n)
{
    const char *s = p;

    while (n) {
        long w = write(fd, s, n);

        if (w <= 0)
            return -1;
        s += w;
        n -= (size_t)w;
    }
    return 0;
}

static int flush_out(FILE *f)
{
    int r = 0;

    if (f->wlen) {
        if (write_all(f->fd, f->buf, f->wlen) < 0) {
            f->flags |= F_ERR;
            r = EOF;
        }
        f->wlen = 0;
    }
    return r;
}

static void drop_input(FILE *f)
{
    if (f->rlen > f->rpos || f->ungot >= 0)
        lseek(f->fd, -(off_t)(f->rlen - f->rpos) - (f->ungot >= 0 ? 1 : 0), SEEK_CUR);
    f->rpos = f->rlen = 0;
    f->ungot = -1;
}

static int ensure_buf(FILE *f)
{
    if (f->buf || (f->flags & F_UNBUF))
        return 0;
    f->buf = malloc(BUFSIZ);
    if (!f->buf) {
        f->flags |= F_UNBUF;
        return -1;
    }
    f->bufsize = BUFSIZ;
    return 0;
}

static int fill(FILE *f)
{
    long n;

    if (!(f->flags & F_READ)) {
        f->flags |= F_ERR;
        errno = EBADF;
        return EOF;
    }
    if (f->wlen && flush_out(f))
        return EOF;
    if (f == stdin)
        fflush(NULL);
    ensure_buf(f);
    if (!f->buf) {
        unsigned char c;

        n = read(f->fd, &c, 1);
        if (n == 1)
            return c;
        f->flags |= n == 0 ? F_EOF : F_ERR;
        return EOF;
    }
    n = read(f->fd, f->buf, f->bufsize);
    if (n <= 0) {
        f->flags |= n == 0 ? F_EOF : F_ERR;
        f->rpos = f->rlen = 0;
        return EOF;
    }
    f->rpos = 1;
    f->rlen = (size_t)n;
    return f->buf[0];
}

int fgetc(FILE *f)
{
    if (f->ungot >= 0) {
        int c = f->ungot;

        f->ungot = -1;
        return c;
    }
    if (f->rpos < f->rlen)
        return f->buf[f->rpos++];
    return fill(f);
}

int getc(FILE *f)
{
    return fgetc(f);
}

int getchar(void)
{
    return fgetc(stdin);
}

int ungetc(int c, FILE *f)
{
    if (c == EOF || f->ungot >= 0)
        return EOF;
    f->ungot = (unsigned char)c;
    f->flags &= ~F_EOF;
    return c;
}

char *fgets(char *s, int n, FILE *f)
{
    int i = 0, c = 0;

    if (n <= 0)
        return NULL;
    while (i < n - 1 && c != '\n') {
        c = fgetc(f);
        if (c == EOF)
            break;
        s[i++] = (char)c;
    }
    if (i == 0)
        return NULL;
    s[i] = 0;
    return s;
}

size_t fread(void *vbuf, size_t size, size_t n, FILE *f)
{
    unsigned char *out = vbuf;
    size_t want = size * n, got = 0;

    if (size == 0 || n == 0)
        return 0;
    while (got < want) {
        size_t avail;

        if (f->ungot >= 0) {
            out[got++] = (unsigned char)f->ungot;
            f->ungot = -1;
            continue;
        }
        avail = f->rlen - f->rpos;
        if (avail) {
            size_t take = want - got < avail ? want - got : avail;

            memcpy(out + got, f->buf + f->rpos, take);
            f->rpos += take;
            got += take;
            continue;
        }
        if (f->buf && want - got >= f->bufsize && (f->flags & F_READ) && !f->wlen) {
            long r = read(f->fd, out + got, want - got);

            if (r <= 0) {
                f->flags |= r == 0 ? F_EOF : F_ERR;
                break;
            }
            got += (size_t)r;
            continue;
        }
        {
            int c = fill(f);

            if (c == EOF)
                break;
            out[got++] = (unsigned char)c;
        }
    }
    return got / size;
}

size_t fwrite(const void *vbuf, size_t size, size_t n, FILE *f)
{
    const unsigned char *in = vbuf;
    size_t total = size * n;

    if (total == 0)
        return 0;
    if (!(f->flags & F_WRITE)) {
        f->flags |= F_ERR;
        errno = EBADF;
        return 0;
    }
    if (f->rlen > f->rpos || f->ungot >= 0)
        drop_input(f);
    ensure_buf(f);
    if (!f->buf) {
        if (write_all(f->fd, in, total) < 0) {
            f->flags |= F_ERR;
            return 0;
        }
        return n;
    }
    while (total) {
        size_t room = f->bufsize - f->wlen;

        if (f->wlen == 0 && total >= f->bufsize) {
            if (write_all(f->fd, in, total) < 0) {
                f->flags |= F_ERR;
                return 0;
            }
            return n;
        }
        if (room > total)
            room = total;
        memcpy(f->buf + f->wlen, in, room);
        f->wlen += room;
        in += room;
        total -= room;
        if (f->wlen == f->bufsize && flush_out(f))
            return 0;
    }
    return n;
}

int fputc(int c, FILE *f)
{
    unsigned char ch = (unsigned char)c;

    return fwrite(&ch, 1, 1, f) == 1 ? c : EOF;
}

int putc(int c, FILE *f)
{
    return fputc(c, f);
}

int putchar(int c)
{
    return fputc(c, stdout);
}

int fputs(const char *s, FILE *f)
{
    size_t n = strlen(s);

    return n == 0 || fwrite(s, 1, n, f) == n ? 0 : EOF;
}

int puts(const char *s)
{
    return fputs(s, stdout) == 0 && fputc('\n', stdout) != EOF ? 0 : EOF;
}

int fflush(FILE *f)
{
    int r = 0;

    if (!f) {
        for (f = open_files; f; f = f->next)
            if (fflush(f))
                r = EOF;
        return r;
    }
    if (f->wlen)
        return flush_out(f);
    if (f->rlen > f->rpos)
        drop_input(f);
    return 0;
}

int fseek(FILE *f, long off, int whence)
{
    if (f->wlen && flush_out(f))
        return -1;
    if (whence == SEEK_CUR)
        off -= (long)(f->rlen - f->rpos) + (f->ungot >= 0 ? 1 : 0);
    f->rpos = f->rlen = 0;
    f->ungot = -1;
    f->flags &= ~F_EOF;
    return lseek(f->fd, off, whence) < 0 ? -1 : 0;
}

long ftell(FILE *f)
{
    off_t pos = lseek(f->fd, 0, SEEK_CUR);

    if (pos < 0)
        return -1;
    pos -= (off_t)(f->rlen - f->rpos) + (f->ungot >= 0 ? 1 : 0);
    pos += (off_t)f->wlen;
    return (long)pos;
}

void rewind(FILE *f)
{
    fseek(f, 0, SEEK_SET);
    f->flags &= ~F_ERR;
}

int feof(FILE *f)
{
    return (f->flags & F_EOF) != 0;
}

int ferror(FILE *f)
{
    return (f->flags & F_ERR) != 0;
}

void clearerr(FILE *f)
{
    f->flags &= ~(F_EOF | F_ERR);
}

int fileno(FILE *f)
{
    return f->fd;
}

int setvbuf(FILE *f, char *buf, int mode, size_t size)
{
    (void)buf;
    (void)size;
    if (mode == _IONBF) {
        fflush(f);
        f->flags |= F_UNBUF;
    }
    return 0;
}

void setbuf(FILE *f, char *buf)
{
    setvbuf(f, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

static FILE *new_file(int fd, int flags)
{
    FILE *f = calloc(1, sizeof(*f));

    if (!f)
        return NULL;
    f->fd = fd;
    f->flags = flags;
    f->ungot = -1;
    f->next = open_files;
    open_files = f;
    return f;
}

static int parse_mode(const char *mode, int *oflags, int *fflags)
{
    int plus = strchr(mode, '+') != NULL;

    switch (mode[0]) {
    case 'r':
        *oflags = plus ? O_RDWR : O_RDONLY;
        *fflags = F_READ | (plus ? F_WRITE : 0);
        return 0;
    case 'w':
        *oflags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
        *fflags = F_WRITE | (plus ? F_READ : 0);
        return 0;
    case 'a':
        *oflags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
        *fflags = F_WRITE | (plus ? F_READ : 0);
        return 0;
    }
    errno = EINVAL;
    return -1;
}

FILE *fopen(const char *path, const char *mode)
{
    int oflags, fflags, fd;
    FILE *f;

    if (parse_mode(mode, &oflags, &fflags) < 0)
        return NULL;
    fd = open(path, oflags, 0666);
    if (fd < 0)
        return NULL;
    f = new_file(fd, fflags);
    if (!f)
        close(fd);
    return f;
}

FILE *fdopen(int fd, const char *mode)
{
    int oflags, fflags;

    if (parse_mode(mode, &oflags, &fflags) < 0)
        return NULL;
    return new_file(fd, fflags);
}

static int release(FILE *f)
{
    FILE **pp;
    int r = fflush(f);

    if (close(f->fd) < 0)
        r = EOF;
    for (pp = &open_files; *pp; pp = &(*pp)->next)
        if (*pp == f) {
            *pp = f->next;
            break;
        }
    free(f->buf);
    free(f);
    return r;
}

int fclose(FILE *f)
{
    return release(f);
}

FILE *popen(const char *cmd, const char *mode)
{
    int fds[2], child_end, my_end;
    int reading = mode[0] == 'r';
    pid_t pid;
    FILE *f;

    if (mode[0] != 'r' && mode[0] != 'w') {
        errno = EINVAL;
        return NULL;
    }
    if (pipe(fds) < 0)
        return NULL;
    my_end = reading ? fds[0] : fds[1];
    child_end = reading ? fds[1] : fds[0];
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }
    if (pid == 0) {
        FILE *o;

        dup2(child_end, reading ? 1 : 0);
        close(fds[0]);
        close(fds[1]);
        for (o = open_files; o; o = o->next)
            if (o->pid)
                close(o->fd);
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        _exit(127);
    }
    close(child_end);
    f = new_file(my_end, reading ? F_READ : F_WRITE);
    if (!f) {
        close(my_end);
        return NULL;
    }
    f->pid = pid;
    return f;
}

int pclose(FILE *f)
{
    pid_t pid = f->pid;
    int status = 0;

    release(f);
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return status;
}

int remove(const char *path)
{
    if (unlink(path) == 0)
        return 0;
    return rmdir(path);
}

void perror(const char *msg)
{
    if (msg && *msg)
        dprintf(2, "%s: %s\n", msg, strerror(errno));
    else
        dprintf(2, "%s\n", strerror(errno));
}

void __stdio_exit(void)
{
    fflush(NULL);
}

struct out {
    FILE  *fp;
    int    fd;
    char  *dst;
    size_t cap, len;
    char   tmp[256];
    size_t tn;
};

static void out_flush(struct out *o)
{
    if (o->tn) {
        if (o->fp)
            fwrite(o->tmp, 1, o->tn, o->fp);
        else
            write_all(o->fd, o->tmp, o->tn);
        o->tn = 0;
    }
}

static void out_char(struct out *o, char c)
{
    if (o->dst) {
        if (o->len + 1 < o->cap)
            o->dst[o->len] = c;
        o->len++;
        return;
    }
    o->tmp[o->tn++] = c;
    o->len++;
    if (o->tn == sizeof(o->tmp))
        out_flush(o);
}

static void pad(struct out *o, int n, char c)
{
    while (n-- > 0)
        out_char(o, c);
}

static void out_field(struct out *o, const char *s, size_t len, int width, int left, char padc)
{
    int fill_n = width > (int)len ? width - (int)len : 0;
    size_t i;

    if (!left)
        pad(o, fill_n, padc);
    for (i = 0; i < len; i++)
        out_char(o, s[i]);
    if (left)
        pad(o, fill_n, ' ');
}

static void out_num(struct out *o, unsigned long v, unsigned base, int upper, int neg,
                    int width, int left, int zero, const char *prefix)
{
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char t[24], tmp[40];
    int n = 0, len = 0, i, plen = prefix ? (int)strlen(prefix) : 0;

    if (v == 0)
        t[n++] = '0';
    while (v) {
        t[n++] = dig[v % base];
        v /= base;
    }
    if (neg)
        tmp[len++] = '-';
    for (i = 0; i < plen; i++)
        tmp[len++] = prefix[i];
    for (i = n; i > 0; i--)
        tmp[len++] = t[i - 1];
    if (zero && !left && (neg || plen) && width > len) {
        int sign = neg ? 1 : plen;

        for (i = 0; i < sign; i++)
            out_char(o, tmp[i]);
        pad(o, width - len, '0');
        out_field(o, tmp + sign, (size_t)len - (size_t)sign, 0, 0, ' ');
        return;
    }
    out_field(o, tmp, (size_t)len, width, left, zero && !left ? '0' : ' ');
}

static void format(struct out *o, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        int lmod = 0, left = 0, zero = 0, width = 0, prec = -1;

        if (*fmt != '%') {
            out_char(o, *fmt);
            continue;
        }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                zero = 1;
            else
                break;
        }
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9')
                    prec = prec * 10 + (*fmt++ - '0');
            }
        }
        while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h' || *fmt == 'j' || *fmt == 't') {
            if (*fmt == 'l' || *fmt == 'z' || *fmt == 'j' || *fmt == 't')
                lmod = 1;
            fmt++;
        }
        switch (*fmt) {
        case 'c': {
            char c = (char)va_arg(ap, int);

            out_field(o, &c, 1, width, left, ' ');
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            size_t len = 0;

            if (!s)
                s = "(null)";
            while (s[len] && (prec < 0 || (int)len < prec))
                len++;
            out_field(o, s, len, width, left, ' ');
            break;
        }
        case 'd':
        case 'i': {
            long v = lmod ? va_arg(ap, long) : va_arg(ap, int);

            out_num(o, v < 0 ? 0UL - (unsigned long)v : (unsigned long)v, 10, 0, v < 0,
                    width, left, zero, NULL);
            break;
        }
        case 'u':
            out_num(o, lmod ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 10, 0, 0,
                    width, left, zero, NULL);
            break;
        case 'x':
            out_num(o, lmod ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 16, 0, 0,
                    width, left, zero, NULL);
            break;
        case 'X':
            out_num(o, lmod ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 16, 1, 0,
                    width, left, zero, NULL);
            break;
        case 'o':
            out_num(o, lmod ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 8, 0, 0,
                    width, left, zero, NULL);
            break;
        case 'p':
            out_num(o, (unsigned long)va_arg(ap, void *), 16, 0, 0, width, left, zero, "0x");
            break;
        case '%':
            out_char(o, '%');
            break;
        case '\0':
            return;
        default:
            out_char(o, '%');
            out_char(o, *fmt);
            break;
        }
    }
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    struct out o;

    memset(&o, 0, sizeof(o));
    o.fp = f;
    format(&o, fmt, ap);
    out_flush(&o);
    return (int)o.len;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct out o;

    memset(&o, 0, sizeof(o));
    o.dst = buf;
    o.cap = size;
    format(&o, fmt, ap);
    if (size)
        buf[o.len < size ? o.len : size - 1] = 0;
    return (int)o.len;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    return vsnprintf(buf, (size_t)-1 >> 1, fmt, ap);
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int dprintf(int fd, const char *fmt, ...)
{
    struct out o;
    va_list ap;

    memset(&o, 0, sizeof(o));
    o.fd = fd;
    va_start(ap, fmt);
    format(&o, fmt, ap);
    va_end(ap);
    out_flush(&o);
    return (int)o.len;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
