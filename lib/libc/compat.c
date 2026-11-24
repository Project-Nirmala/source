#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <setjmp.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <termios.h>

__asm__(
    ".text\n"
    ".globl setjmp\n"
    "setjmp:\n"
    "movq %rbx, 0(%rdi)\n"
    "movq %rbp, 8(%rdi)\n"
    "movq %r12, 16(%rdi)\n"
    "movq %r13, 24(%rdi)\n"
    "movq %r14, 32(%rdi)\n"
    "movq %r15, 40(%rdi)\n"
    "leaq 8(%rsp), %rdx\n"
    "movq %rdx, 48(%rdi)\n"
    "movq (%rsp), %rdx\n"
    "movq %rdx, 56(%rdi)\n"
    "xorl %eax, %eax\n"
    "ret\n"
    ".globl longjmp\n"
    "longjmp:\n"
    "movl %esi, %eax\n"
    "testl %eax, %eax\n"
    "jnz 1f\n"
    "movl $1, %eax\n"
    "1:\n"
    "movq 0(%rdi), %rbx\n"
    "movq 8(%rdi), %rbp\n"
    "movq 16(%rdi), %r12\n"
    "movq 24(%rdi), %r13\n"
    "movq 32(%rdi), %r14\n"
    "movq 40(%rdi), %r15\n"
    "movq 48(%rdi), %rsp\n"
    "jmp *56(%rdi)\n");

int sigemptyset(sigset_t *s) { *s = 0; return 0; }
int sigfillset(sigset_t *s) { *s = ~0UL; return 0; }
int sigaddset(sigset_t *s, int sig) { *s |= 1UL << sig; return 0; }
int sigdelset(sigset_t *s, int sig) { *s &= ~(1UL << sig); return 0; }
int sigismember(const sigset_t *s, int sig) { return (*s >> sig) & 1; }

struct ksigaction {
    unsigned long handler;
    unsigned long flags;
    unsigned long restorer;
    unsigned long mask;
};

__asm__(
    ".text\n"
    ".globl __sigreturn_tramp\n"
    "__sigreturn_tramp:\n"
    "movl $15, %eax\n"
    "syscall\n");

extern void __sigreturn_tramp(void);

static long sys4(long n, long a, long b, long c, long d)
{
    long r;
    register long r10 __asm__("r10") = d;

    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "0"(n), "D"(a), "S"(b), "d"(c), "r"(r10)
                     : "rcx", "r11", "memory");
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *old)
{
    return (int)sys4(14, how, (long)set, (long)old, 8);
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *old)
{
    struct ksigaction k, o;

    if (act) {
        k.handler = (unsigned long)act->sa_handler;
        k.flags = 0;
        k.restorer = (unsigned long)__sigreturn_tramp;
        k.mask = act->sa_mask;
    }
    if (sys4(13, sig, act ? (long)&k : 0, old ? (long)&o : 0, 8) < 0)
        return -1;
    if (old) {
        old->sa_handler = (sighandler_t)o.handler;
        old->sa_mask = o.mask;
        old->sa_flags = 0;
    }
    return 0;
}

sighandler_t signal(int sig, sighandler_t handler)
{
    struct sigaction a, o;

    a.sa_handler = handler;
    a.sa_mask = 0;
    a.sa_flags = 0;
    if (sigaction(sig, &a, &o) < 0)
        return SIG_ERR;
    return o.sa_handler;
}

int raise(int sig)
{
    return kill(getpid(), sig);
}

pid_t getpgrp(void) { return (pid_t)sys4(111, 0, 0, 0, 0); }
int setpgid(pid_t pid, pid_t pgid) { return (int)sys4(109, pid, pgid, 0, 0); }
pid_t tcgetpgrp(int fd)
{
    int pgrp;

    if (ioctl(fd, TIOCGPGRP, &pgrp) < 0)
        return -1;
    return (pid_t)pgrp;
}

int tcsetpgrp(int fd, pid_t pgrp)
{
    int v = (int)pgrp;

    return ioctl(fd, TIOCSPGRP, &v);
}

int getrlimit(int res, struct rlimit *rl)
{
    rl->rlim_cur = RLIM_INFINITY;
    rl->rlim_max = RLIM_INFINITY;
    if (res == RLIMIT_NOFILE) {
        rl->rlim_cur = 128;
        rl->rlim_max = 128;
    } else if (res == RLIMIT_STACK) {
        rl->rlim_cur = 8UL << 20;
    } else if (res == RLIMIT_CORE) {
        rl->rlim_cur = 0;
    }
    return 0;
}

int setrlimit(int res, const struct rlimit *rl)
{
    (void)res;
    (void)rl;
    return 0;
}

DIR *opendir(const char *path)
{
    DIR *d;
    int fd;

    fd = open(path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0)
        return 0;
    d = malloc(sizeof(*d));
    if (!d) {
        close(fd);
        errno = ENOMEM;
        return 0;
    }
    d->fd = fd;
    d->pos = 0;
    d->len = 0;
    return d;
}

struct dirent *readdir(DIR *d)
{
    struct dirent *e;

    if (d->pos >= d->len) {
        long n = getdents64(d->fd, d->buf, sizeof(d->buf));

        if (n <= 0)
            return 0;
        d->pos = 0;
        d->len = n;
    }
    e = (struct dirent *)(d->buf + d->pos);
    d->pos += e->d_reclen;
    return e;
}

DIR *fdopendir(int fd)
{
    DIR *d = malloc(sizeof(*d));

    if (!d) {
        errno = ENOMEM;
        return 0;
    }
    d->fd = fd;
    d->pos = 0;
    d->len = 0;
    return d;
}

int closedir(DIR *d)
{
    int r = close(d->fd);

    free(d);
    return r;
}

long long strtoll(const char *s, char **end, int base)
{
    return strtol(s, end, base);
}

char *strtok(char *s, const char *delim)
{
    static char *save;
    char *tok;

    if (!s)
        s = save;
    if (!s)
        return 0;
    while (*s && strchr(delim, *s))
        s++;
    if (!*s) {
        save = 0;
        return 0;
    }
    tok = s;
    while (*s && !strchr(delim, *s))
        s++;
    if (*s)
        *s++ = 0;
    save = *s || s > tok ? s : 0;
    return tok;
}

char *stpncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;

    while (i < n && s[i]) {
        d[i] = s[i];
        i++;
    }
    for (size_t j = i; j < n; j++)
        d[j] = 0;
    return d + i;
}

char *optarg;
int optind = 1, opterr = 1, optopt;

int getopt(int argc, char *const argv[], const char *opts)
{
    static int pos = 1;
    const char *p;
    char c;

    optarg = 0;
    if (optind >= argc || argv[optind][0] != '-' || !argv[optind][1])
        return -1;
    if (argv[optind][1] == '-' && !argv[optind][2]) {
        optind++;
        return -1;
    }
    c = argv[optind][pos];
    p = strchr(opts, c);
    if (!p || c == ':') {
        optopt = c;
        if (!argv[optind][++pos]) {
            optind++;
            pos = 1;
        }
        return '?';
    }
    if (p[1] == ':') {
        if (argv[optind][pos + 1]) {
            optarg = (char *)&argv[optind][pos + 1];
        } else if (optind + 1 < argc) {
            optarg = argv[++optind];
        } else {
            optopt = c;
            optind++;
            pos = 1;
            return '?';
        }
        optind++;
        pos = 1;
        return c;
    }
    if (!argv[optind][++pos]) {
        optind++;
        pos = 1;
    }
    return c;
}

#include <inttypes.h>
#include <pwd.h>
#include <sys/wait.h>

void *mempcpy(void *d, const void *s, size_t n)
{
    memcpy(d, s, n);
    return (char *)d + n;
}

char *stpcpy(char *d, const char *s)
{
    size_t n = strlen(s);

    memcpy(d, s, n + 1);
    return d + n;
}

char *strchrnul(const char *s, int c)
{
    while (*s && *s != (char)c)
        s++;
    return (char *)s;
}

char *strsignal(int sig)
{
    static char buf[24];
    char *p = buf + sizeof(buf) - 1;
    unsigned v = sig < 0 ? 0 : (unsigned)sig;

    *p = 0;
    do {
        *--p = '0' + v % 10;
        v /= 10;
    } while (v);
    return p;
}

intmax_t strtoimax(const char *s, char **end, int base)
{
    return strtol(s, end, base);
}

int sigsuspend(const sigset_t *mask)
{
    (void)mask;
    sched_yield();
    errno = EINTR;
    return -1;
}

pid_t wait3(int *status, int options, void *usage)
{
    (void)usage;
    return waitpid(-1, status, options);
}


#include <sys/times.h>

uintmax_t strtoumax(const char *s, char **end, int base)
{
    return strtoul(s, end, base);
}

clock_t times(struct tms *buf)
{
    buf->tms_utime = 0;
    buf->tms_stime = 0;
    buf->tms_cutime = 0;
    buf->tms_cstime = 0;
    return 0;
}

long sysconf(int name)
{
    return name == _SC_CLK_TCK ? 100 : -1;
}

int getgroups(int n, gid_t *list)
{
    (void)n;
    (void)list;
    return 0;
}

#include <stdio.h>

void __assert_fail(const char *expr, const char *file, int line)
{
    fprintf(stderr, "assertion failed: %s (%s:%d)\n", expr, file, line);
    abort();
}

int flock(int fd, int op)
{
    (void)fd;
    (void)op;
    return 0;
}

#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <utime.h>
#include <time.h>

int fcntl(int fd, int cmd, ...)
{
    va_list ap;
    long arg;

    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);
    return (int)sys4(72, fd, cmd, arg, 0);
}

int fchdir(int fd) { return (int)sys4(81, fd, 0, 0, 0); }
pid_t setsid(void) { return (pid_t)sys4(112, 0, 0, 0, 0); }
int killpg(pid_t pgrp, int sig) { return kill(-pgrp, sig); }

int utimes(const char *path, const struct timeval tv[2])
{
    struct utimbuf u;

    if (!tv)
        return utime(path, 0);
    u.actime = tv[0].tv_sec;
    u.modtime = tv[1].tv_sec;
    return utime(path, &u);
}

ssize_t readv(int fd, const struct iovec *iov, int cnt)
{
    ssize_t total = 0;

    for (int i = 0; i < cnt; i++) {
        ssize_t r = read(fd, iov[i].iov_base, iov[i].iov_len);

        if (r < 0)
            return total ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len)
            break;
    }
    return total;
}

ssize_t writev(int fd, const struct iovec *iov, int cnt)
{
    ssize_t total = 0;

    for (int i = 0; i < cnt; i++) {
        ssize_t r = write(fd, iov[i].iov_base, iov[i].iov_len);

        if (r < 0)
            return total ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len)
            break;
    }
    return total;
}

ssize_t pread(int fd, void *buf, size_t n, off_t off)
{
    off_t cur = lseek(fd, 0, SEEK_CUR);
    ssize_t r;

    if (cur < 0 || lseek(fd, off, SEEK_SET) < 0)
        return -1;
    r = read(fd, buf, n);
    lseek(fd, cur, SEEK_SET);
    return r;
}

ssize_t pwrite(int fd, const void *buf, size_t n, off_t off)
{
    off_t cur = lseek(fd, 0, SEEK_CUR);
    ssize_t r;

    if (cur < 0 || lseek(fd, off, SEEK_SET) < 0)
        return -1;
    r = write(fd, buf, n);
    lseek(fd, cur, SEEK_SET);
    return r;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    void *p;

    (void)addr;
    (void)prot;
    (void)fd;
    (void)off;
    if (!(flags & MAP_ANONYMOUS) || !len) {
        errno = ENOSYS;
        return MAP_FAILED;
    }
    p = calloc(1, len);
    if (!p) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return p;
}

int munmap(void *addr, size_t len)
{
    (void)len;
    free(addr);
    return 0;
}

int mprotect(void *addr, size_t len, int prot)
{
    (void)addr;
    (void)len;
    (void)prot;
    return 0;
}

int ffs(int v)
{
    int i;

    if (!v)
        return 0;
    for (i = 1; !(v & 1); i++)
        v >>= 1;
    return i;
}

static unsigned long rnd_state = 1;

void srandom(unsigned seed)
{
    rnd_state = seed ? seed : 1;
}

long random(void)
{
    rnd_state = rnd_state * 6364136223846793005UL + 1442695040888963407UL;
    return (long)((rnd_state >> 33) & 0x7FFFFFFF);
}

time_t mktime(struct tm *tm)
{
    time_t guess = timegm(tm);
    struct tm lt;
    time_t off;

    localtime_r(&guess, &lt);
    off = timegm(&lt) - guess;
    return guess - off;
}

static const char *const wday_name[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const mon_name[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

static size_t put_num(char *s, size_t max, int v, int width, char pad)
{
    char tmp[16];
    int n = 0, neg = v < 0;
    size_t len = 0, i;

    if (neg)
        v = -v;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n < width)
        tmp[n++] = pad;
    if (neg)
        tmp[n++] = '-';
    for (i = 0; i < (size_t)n && len < max; i++)
        s[len++] = tmp[n - 1 - (int)i];
    return len;
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
    size_t len = 0;

    while (*fmt && len + 1 < max) {
        const char *str = 0;
        size_t room = max - 1 - len;
        char c = *fmt++;
        int h12;

        if (c != '%') {
            s[len++] = c;
            continue;
        }
        c = *fmt++;
        if (c == 'E' || c == 'O')
            c = *fmt++;
        h12 = tm->tm_hour % 12 ? tm->tm_hour % 12 : 12;
        switch (c) {
        case 'Y': len += put_num(s + len, room, tm->tm_year + 1900, 1, '0'); break;
        case 'y': len += put_num(s + len, room, tm->tm_year % 100, 2, '0'); break;
        case 'C': len += put_num(s + len, room, (tm->tm_year + 1900) / 100, 2, '0'); break;
        case 'm': len += put_num(s + len, room, tm->tm_mon + 1, 2, '0'); break;
        case 'd': len += put_num(s + len, room, tm->tm_mday, 2, '0'); break;
        case 'e': len += put_num(s + len, room, tm->tm_mday, 2, ' '); break;
        case 'H': len += put_num(s + len, room, tm->tm_hour, 2, '0'); break;
        case 'I': len += put_num(s + len, room, h12, 2, '0'); break;
        case 'M': len += put_num(s + len, room, tm->tm_min, 2, '0'); break;
        case 'S': len += put_num(s + len, room, tm->tm_sec, 2, '0'); break;
        case 'j': len += put_num(s + len, room, tm->tm_yday + 1, 3, '0'); break;
        case 'u': len += put_num(s + len, room, tm->tm_wday ? tm->tm_wday : 7, 1, '0'); break;
        case 'w': len += put_num(s + len, room, tm->tm_wday, 1, '0'); break;
        case 'p': str = tm->tm_hour < 12 ? "AM" : "PM"; break;
        case 'a': case 'A': str = wday_name[tm->tm_wday % 7]; break;
        case 'b': case 'B': case 'h': str = mon_name[tm->tm_mon % 12]; break;
        case 'Z': str = "UTC"; break;
        case 'z': str = "+0000"; break;
        case 'n': str = "\n"; break;
        case 't': str = "\t"; break;
        case '%': str = "%"; break;
        case 'F': len += strftime(s + len, max - len, "%Y-%m-%d", tm); break;
        case 'T': len += strftime(s + len, max - len, "%H:%M:%S", tm); break;
        case 'R': len += strftime(s + len, max - len, "%H:%M", tm); break;
        case 'D': len += strftime(s + len, max - len, "%m/%d/%y", tm); break;
        case 'c': len += strftime(s + len, max - len, "%a %b %e %H:%M:%S %Y", tm); break;
        case 'x': len += strftime(s + len, max - len, "%m/%d/%y", tm); break;
        case 'X': len += strftime(s + len, max - len, "%H:%M:%S", tm); break;
        default: return 0;
        }
        if (str) {
            size_t n = strlen(str);

            if (c == 'a' || c == 'b' || c == 'h')
                n = 3;
            if (n > room)
                return 0;
            memcpy(s + len, str, n);
            len += n;
        }
    }
    s[len] = 0;
    return len;
}

#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>

static void tmpl_fill(char *tail, unsigned long *seed)
{
    static const char set[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    int i;

    for (i = 0; i < 6; i++) {
        *seed = *seed * 6364136223846793005UL + 1442695040888963407UL;
        tail[i] = set[(*seed >> 33) % (sizeof(set) - 1)];
    }
}

static int tmpl_check(char *tmpl, char **tail)
{
    size_t n = strlen(tmpl);

    if (n < 6 || strcmp(tmpl + n - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return -1;
    }
    *tail = tmpl + n - 6;
    return 0;
}

int mkstemp(char *tmpl)
{
    char *tail;
    unsigned long seed = (unsigned long)time(0) ^ ((unsigned long)getpid() << 16) ^ (unsigned long)tmpl;

    if (tmpl_check(tmpl, &tail) < 0)
        return -1;
    for (int tries = 0; tries < 100; tries++) {
        int fd;

        tmpl_fill(tail, &seed);
        fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST)
            return fd;
    }
    return -1;
}

char *mkdtemp(char *tmpl)
{
    char *tail;
    unsigned long seed = (unsigned long)time(0) ^ ((unsigned long)getpid() << 16) ^ (unsigned long)tmpl;

    if (tmpl_check(tmpl, &tail) < 0)
        return 0;
    for (int tries = 0; tries < 100; tries++) {
        tmpl_fill(tail, &seed);
        if (mkdir(tmpl, 0700) == 0)
            return tmpl;
        if (errno != EEXIST)
            return 0;
    }
    return 0;
}

char *realpath(const char *path, char *resolved)
{
    char out[PATH_MAX];
    struct stat st;
    size_t olen = 0;
    const char *p;

    if (!path || !resolved) {
        errno = EINVAL;
        return 0;
    }
    if (path[0] != '/') {
        if (!getcwd(out, sizeof(out)))
            return 0;
        olen = strlen(out);
        if (olen == 1)
            olen = 0;
    }
    if (strlen(path) + olen + 2 > sizeof(out)) {
        errno = ENAMETOOLONG;
        return 0;
    }
    out[olen] = 0;
    p = path;
    while (*p) {
        size_t n = 0;

        while (*p == '/')
            p++;
        if (!*p)
            break;
        while (p[n] && p[n] != '/')
            n++;
        if (n == 1 && p[0] == '.') {
        } else if (n == 2 && p[0] == '.' && p[1] == '.') {
            while (olen && out[olen - 1] != '/')
                olen--;
            if (olen)
                olen--;
            out[olen] = 0;
        } else {
            if (olen + n + 2 > sizeof(out)) {
                errno = ENAMETOOLONG;
                return 0;
            }
            out[olen++] = '/';
            memcpy(out + olen, p, n);
            olen += n;
            out[olen] = 0;
        }
        p += n;
    }
    if (!olen) {
        out[0] = '/';
        out[1] = 0;
    }
    if (stat(out, &st) < 0)
        return 0;
    strcpy(resolved, out);
    return resolved;
}

static int scan_digit(int c, int base)
{
    int v;

    if (c >= '0' && c <= '9')
        v = c - '0';
    else if (c >= 'a' && c <= 'f')
        v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        v = c - 'A' + 10;
    else
        return -1;
    return v < base ? v : -1;
}

int vsscanf(const char *s, const char *fmt, va_list ap)
{
    const char *start = s;
    int count = 0;

    while (*fmt) {
        int lng = 0, suppress = 0, width = 0;

        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*s))
                s++;
            fmt++;
            continue;
        }
        if (*fmt != '%') {
            if (*s != *fmt)
                return count;
            s++;
            fmt++;
            continue;
        }
        fmt++;
        if (*fmt == '*') {
            suppress = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z') {
            if (*fmt == 'l' || *fmt == 'z')
                lng++;
            fmt++;
        }
        if (*fmt == '%') {
            if (*s != '%')
                return count;
            s++;
            fmt++;
            continue;
        }
        if (*fmt == 'n') {
            if (!suppress)
                *va_arg(ap, int *) = (int)(s - start);
            fmt++;
            continue;
        }
        if (*fmt != 'c' && *fmt != '[')
            while (isspace((unsigned char)*s))
                s++;
        if (!*s)
            return count ? count : -1;
        if (*fmt == 'd' || *fmt == 'i' || *fmt == 'u' || *fmt == 'x' || *fmt == 'X' || *fmt == 'o') {
            int base = *fmt == 'x' || *fmt == 'X' ? 16 : (*fmt == 'o' ? 8 : 10);
            int neg = 0, got = 0, w = width ? width : 1 << 30;
            unsigned long v = 0;

            if ((*s == '-' || *s == '+') && w > 0) {
                neg = *s == '-';
                s++;
                w--;
            }
            if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && scan_digit(s[2], 16) >= 0) {
                s += 2;
                w -= 2;
            }
            while (w > 0 && scan_digit(*s, base) >= 0) {
                v = v * (unsigned long)base + (unsigned long)scan_digit(*s, base);
                s++;
                w--;
                got = 1;
            }
            if (!got)
                return count;
            if (!suppress) {
                long sv = neg ? -(long)v : (long)v;

                if (lng)
                    *va_arg(ap, long *) = sv;
                else
                    *va_arg(ap, int *) = (int)sv;
                count++;
            }
        } else if (*fmt == 's') {
            char *d = suppress ? 0 : va_arg(ap, char *);
            int w = width ? width : 1 << 30;

            while (*s && !isspace((unsigned char)*s) && w-- > 0) {
                if (d)
                    *d++ = *s;
                s++;
            }
            if (d) {
                *d = 0;
                count++;
            }
        } else if (*fmt == 'c') {
            char *d = suppress ? 0 : va_arg(ap, char *);
            int w = width ? width : 1;

            while (*s && w-- > 0) {
                if (d)
                    *d++ = *s;
                s++;
            }
            if (d)
                count++;
        } else if (*fmt == '[') {
            char set[256] = {0};
            int neg = 0, w = width ? width : 1 << 30;
            char *d;

            fmt++;
            if (*fmt == '^') {
                neg = 1;
                fmt++;
            }
            if (*fmt == ']')
                set[(unsigned char)*fmt++] = 1;
            while (*fmt && *fmt != ']') {
                if (fmt[1] == '-' && fmt[2] && fmt[2] != ']') {
                    for (int c = (unsigned char)fmt[0]; c <= (unsigned char)fmt[2]; c++)
                        set[c] = 1;
                    fmt += 3;
                } else {
                    set[(unsigned char)*fmt++] = 1;
                }
            }
            d = suppress ? 0 : va_arg(ap, char *);
            {
                const char *b = s;

                while (*s && w-- > 0 && (set[(unsigned char)*s] != 0) != neg) {
                    if (d)
                        *d++ = *s;
                    s++;
                }
                if (s == b)
                    return count;
            }
            if (d) {
                *d = 0;
                count++;
            }
        } else {
            return count;
        }
        fmt++;
    }
    return count;
}

int sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}
