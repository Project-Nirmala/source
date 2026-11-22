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

int fcntl(int fd, int cmd, ...)
{
    va_list ap;
    long arg;
    int held[64];
    int n = 0, r;

    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);
    if (cmd != F_DUPFD)
        return 0;
    for (;;) {
        r = dup(fd);
        if (r < 0 || r >= arg)
            break;
        if (n >= 64) {
            close(r);
            errno = EINVAL;
            r = -1;
            break;
        }
        held[n++] = r;
    }
    while (n)
        close(held[--n]);
    return r;
}

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
pid_t tcgetpgrp(int fd) { (void)fd; return getpid(); }
int tcsetpgrp(int fd, pid_t pgrp) { (void)fd; (void)pgrp; return 0; }

int getrlimit(int res, struct rlimit *rl)
{
    (void)res;
    (void)rl;
    errno = ENOSYS;
    return -1;
}

int setrlimit(int res, const struct rlimit *rl)
{
    (void)res;
    (void)rl;
    errno = ENOSYS;
    return -1;
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

struct passwd *getpwnam(const char *name)
{
    (void)name;
    return 0;
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
