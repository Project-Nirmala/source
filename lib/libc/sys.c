#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <utime.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/wait.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/statfs.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/sysinfo.h>
#include <sys/syscall.h>

int errno;

static long raw(long n, long a, long b, long c)
{
    long r;

    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "0"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return r;
}

static long raw4(long n, long a, long b, long c, long d)
{
    long r;
    register long r10 __asm__("r10") = d;

    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "0"(n), "D"(a), "S"(b), "d"(c), "r"(r10)
                     : "rcx", "r11", "memory");
    return r;
}

static long fix(long r)
{
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

ssize_t read(int fd, void *buf, size_t n)
{
    return fix(raw(SYS_read, fd, (long)buf, (long)n));
}

ssize_t write(int fd, const void *buf, size_t n)
{
    return fix(raw(SYS_write, fd, (long)buf, (long)n));
}

int open(const char *path, int flags, ...)
{
    va_list ap;
    int mode = 0;

    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    return (int)fix(raw(SYS_open, (long)path, flags, mode));
}

int close(int fd)
{
    return (int)fix(raw(SYS_close, fd, 0, 0));
}

off_t lseek(int fd, off_t off, int whence)
{
    return fix(raw(SYS_lseek, fd, off, whence));
}

pid_t getpid(void)
{
    return (pid_t)raw(SYS_getpid, 0, 0, 0);
}

int sched_yield(void)
{
    return (int)fix(raw(SYS_sched_yield, 0, 0, 0));
}

int usleep(unsigned usec)
{
    long ts[2];

    ts[0] = usec / 1000000;
    ts[1] = (long)(usec % 1000000) * 1000;
    return (int)fix(raw(SYS_nanosleep, (long)ts, 0, 0));
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    return (int)fix(raw(SYS_execve, (long)path, (long)argv, (long)envp));
}

void _exit(int status)
{
    raw(SYS_exit, status, 0, 0);
    for (;;)
        ;
}

int ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    unsigned long arg;

    va_start(ap, req);
    arg = va_arg(ap, unsigned long);
    va_end(ap);
    return (int)fix(raw(SYS_ioctl, fd, (long)req, (long)arg));
}

int tcgetattr(int fd, struct termios *t)
{
    return ioctl(fd, TCGETS, t);
}

int tcsetattr(int fd, int action, const struct termios *t)
{
    return ioctl(fd, TCSETS + action, t);
}

pid_t getppid(void)
{
    return (pid_t)raw(SYS_getppid, 0, 0, 0);
}

pid_t fork(void)
{
    return (pid_t)fix(raw(SYS_fork, 0, 0, 0));
}

int pipe(int fds[2])
{
    return (int)fix(raw(SYS_pipe, (long)fds, 0, 0));
}

int dup(int fd)
{
    return (int)fix(raw(SYS_dup, fd, 0, 0));
}

int dup2(int oldfd, int newfd)
{
    return (int)fix(raw(SYS_dup2, oldfd, newfd, 0));
}

int chdir(const char *path)
{
    return (int)fix(raw(SYS_chdir, (long)path, 0, 0));
}

char *getcwd(char *buf, size_t size)
{
    long r = fix(raw(SYS_getcwd, (long)buf, (long)size, 0));

    return r < 0 ? (char *)0 : buf;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    return (pid_t)fix(raw4(SYS_wait4, pid, (long)status, options, 0));
}

pid_t wait(int *status)
{
    return waitpid(-1, status, 0);
}

long getdents64(int fd, void *buf, size_t n)
{
    return fix(raw(SYS_getdents64, fd, (long)buf, (long)n));
}

int sysinfo(struct sysinfo *info)
{
    return (int)fix(raw(SYS_sysinfo, (long)info, 0, 0));
}

int stat(const char *path, struct stat *st)
{
    return (int)fix(raw(SYS_stat, (long)path, (long)st, 0));
}

int lstat(const char *path, struct stat *st)
{
    return (int)fix(raw(SYS_lstat, (long)path, (long)st, 0));
}

int fstat(int fd, struct stat *st)
{
    return (int)fix(raw(SYS_fstat, fd, (long)st, 0));
}

int mkdir(const char *path, mode_t mode)
{
    return (int)fix(raw(SYS_mkdir, (long)path, (long)mode, 0));
}

int rmdir(const char *path)
{
    return (int)fix(raw(SYS_rmdir, (long)path, 0, 0));
}

int unlink(const char *path)
{
    return (int)fix(raw(SYS_unlink, (long)path, 0, 0));
}

int rename(const char *from, const char *to)
{
    return (int)fix(raw(SYS_rename, (long)from, (long)to, 0));
}

int truncate(const char *path, off_t len)
{
    return (int)fix(raw(SYS_truncate, (long)path, len, 0));
}

int ftruncate(int fd, off_t len)
{
    return (int)fix(raw(SYS_ftruncate, fd, len, 0));
}

int access(const char *path, int mode)
{
    return (int)fix(raw(SYS_access, (long)path, mode, 0));
}

void sync(void)
{
    raw(SYS_sync, 0, 0, 0);
}

int uname(struct utsname *u)
{
    return (int)fix(raw(SYS_uname, (long)u, 0, 0));
}

int statfs(const char *path, struct statfs *buf)
{
    return (int)fix(raw(SYS_statfs, (long)path, (long)buf, 0));
}

int kill(pid_t pid, int sig)
{
    return (int)fix(raw(SYS_kill, pid, sig, 0));
}

void *sbrk(long inc)
{
    static long cur;
    long want, got;

    if (!cur)
        cur = raw(SYS_brk, 0, 0, 0);
    if (inc == 0)
        return (void *)cur;
    want = cur + inc;
    got = raw(SYS_brk, want, 0, 0);
    if (got != want) {
        errno = ENOMEM;
        return (void *)-1;
    }
    got = cur;
    cur = want;
    return (void *)got;
}

time_t time(time_t *t)
{
    time_t now = (time_t)raw(SYS_time, 0, 0, 0);

    if (t)
        *t = now;
    return now;
}

int clock_gettime(int clock, struct timespec *ts)
{
    return (int)fix(raw(SYS_clock_gettime, clock, (long)ts, 0));
}

int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    return (int)fix(raw(SYS_gettimeofday, (long)tv, 0, 0));
}

int utime(const char *path, const struct utimbuf *times)
{
    return (int)fix(raw(SYS_utime, (long)path, (long)times, 0));
}

int chmod(const char *path, mode_t mode)
{
    return (int)fix(raw(SYS_chmod, (long)path, (long)mode, 0));
}

mode_t umask(mode_t mask)
{
    return (mode_t)raw(SYS_umask, (long)mask, 0, 0);
}

int execv(const char *path, char *const argv[])
{
    return execve(path, argv, environ);
}

int execvp(const char *file, char *const argv[])
{
    const char *path = getenv("PATH");
    char buf[1024];
    int err = ENOENT;

    if (strchr(file, '/'))
        return execve(file, argv, environ);
    if (!path)
        path = "/bin:/sbin";
    while (*path) {
        size_t dl = 0, fl = strlen(file);

        while (path[dl] && path[dl] != ':')
            dl++;
        if (dl + 1 + fl + 1 <= sizeof(buf)) {
            memcpy(buf, path, dl);
            buf[dl] = '/';
            memcpy(buf + dl + 1, file, fl + 1);
            execve(buf, argv, environ);
            if (errno != ENOENT)
                err = errno;
        }
        path += dl;
        if (*path == ':')
            path++;
    }
    errno = err;
    return -1;
}

int execl(const char *path, const char *arg0, ...)
{
    const char *argv[64];
    va_list ap;
    int n = 0;
    const char *a = arg0;

    va_start(ap, arg0);
    while (a && n < 63) {
        argv[n++] = a;
        a = va_arg(ap, const char *);
    }
    va_end(ap);
    argv[n] = 0;
    return execve(path, (char *const *)argv, environ);
}

static sighandler_t handlers[32];

sighandler_t signal(int sig, sighandler_t handler)
{
    sighandler_t old;

    if (sig < 1 || sig > 31) {
        errno = EINVAL;
        return SIG_ERR;
    }
    old = handlers[sig];
    handlers[sig] = handler;
    return old;
}
