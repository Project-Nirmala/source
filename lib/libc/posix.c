#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <err.h>
#include <sys/ioctl.h>

size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);

    if (size) {
        size_t n = len >= size ? size - 1 : len;

        memcpy(dst, src, n);
        dst[n] = 0;
    }
    return len;
}

char *strndup(const char *s, size_t n)
{
    size_t len = 0;
    char *p;

    while (len < n && s[len])
        len++;
    p = malloc(len + 1);
    if (!p)
        return NULL;
    memcpy(p, s, len);
    p[len] = 0;
    return p;
}

int isatty(int fd)
{
    struct termios t;

    return tcgetattr(fd, &t) == 0;
}

speed_t cfgetospeed(const struct termios *t)
{
    return t->c_cflag & 0x100F;
}

speed_t cfgetispeed(const struct termios *t)
{
    return t->c_cflag & 0x100F;
}

int cfsetospeed(struct termios *t, speed_t s)
{
    t->c_cflag = (t->c_cflag & ~0x100Fu) | (s & 0x100F);
    return 0;
}

int cfsetispeed(struct termios *t, speed_t s)
{
    (void)t;
    (void)s;
    return 0;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    int waited = 0;

    for (;;) {
        nfds_t i;
        int ready = 0;

        for (i = 0; i < nfds; i++) {
            int avail = 0;

            fds[i].revents = 0;
            if (fds[i].fd < 0)
                continue;
            if (fds[i].events & POLLOUT)
                fds[i].revents |= POLLOUT;
            if ((fds[i].events & POLLIN) && ioctl(fds[i].fd, FIONREAD, &avail) == 0 && avail > 0)
                fds[i].revents |= POLLIN;
            if (fds[i].revents)
                ready++;
        }
        if (ready || timeout == 0 || (timeout > 0 && waited >= timeout))
            return ready;
        usleep(5000);
        waited += 5;
    }
}

void vwarn(const char *fmt, va_list ap)
{
    fprintf(stderr, "error: ");
    if (fmt) {
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, ": ");
    }
    fprintf(stderr, "%s\n", strerror(errno));
}

void vwarnx(const char *fmt, va_list ap)
{
    fprintf(stderr, "error: ");
    if (fmt)
        vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

void verr(int code, const char *fmt, va_list ap)
{
    vwarn(fmt, ap);
    exit(code);
}

void verrx(int code, const char *fmt, va_list ap)
{
    vwarnx(fmt, ap);
    exit(code);
}

void warn(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vwarn(fmt, ap);
    va_end(ap);
}

void warnx(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vwarnx(fmt, ap);
    va_end(ap);
}

void err(int code, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    verr(code, fmt, ap);
    va_end(ap);
}

void errx(int code, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    verrx(code, fmt, ap);
    va_end(ap);
}

#include <libgen.h>
#include <string.h>

char *basename(char *path)
{
    char *p;
    size_t n;

    if (!path || !*path)
        return ".";
    n = strlen(path);
    while (n > 1 && path[n - 1] == '/')
        path[--n] = 0;
    if (n == 1 && path[0] == '/')
        return path;
    p = strrchr(path, '/');
    return p ? p + 1 : path;
}

char *dirname(char *path)
{
    char *p;
    size_t n;

    if (!path || !*path)
        return ".";
    n = strlen(path);
    while (n > 1 && path[n - 1] == '/')
        path[--n] = 0;
    p = strrchr(path, '/');
    if (!p)
        return ".";
    while (p > path && p[-1] == '/')
        p--;
    if (p == path)
        path[1] = 0;
    else
        *p = 0;
    return path;
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>

long long llabs(long long v)
{
    return v < 0 ? -v : v;
}

ssize_t getline(char **line, size_t *n, FILE *f)
{
    size_t len = 0;
    int c;

    if (!*line || !*n) {
        *n = 128;
        *line = malloc(*n);
        if (!*line)
            return -1;
    }
    while ((c = fgetc(f)) != EOF) {
        if (len + 2 > *n) {
            char *p = realloc(*line, *n * 2);

            if (!p)
                return -1;
            *line = p;
            *n *= 2;
        }
        (*line)[len++] = c;
        if (c == '\n')
            break;
    }
    if (!len)
        return -1;
    (*line)[len] = 0;
    return len;
}

FILE *fmemopen(void *buf, size_t size, const char *mode)
{
    errno = ENOSYS;
    return 0;
}

unsigned sleep(unsigned sec)
{
    while (sec--)
        usleep(1000000);
    return 0;
}

int link(const char *from, const char *to)
{
    errno = ENOSYS;
    return -1;
}

int chroot(const char *path)
{
    errno = ENOSYS;
    return -1;
}

int clock_settime(int clock, const struct timespec *ts)
{
    errno = ENOSYS;
    return -1;
}

int mkfifo(const char *path, mode_t mode)
{
    errno = ENOSYS;
    return -1;
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
    return chmod(path, mode);
}


char *getlogin(void)
{
    return 0;
}

char *ttyname(int fd)
{
    return 0;
}

#include <stdio.h>
#include <string.h>

static int field(char **p, char **out)
{
    char *c = strchr(*p, ':');

    *out = *p;
    if (!c)
        return 0;
    *c = 0;
    *p = c + 1;
    return 1;
}

static struct passwd *pwscan(const char *name, int id)
{
    static struct passwd pw;
    static char line[512];
    FILE *f = fopen("/etc/passwd", "r");
    char *p, *n, *salt, *hash, *u, *g, *dir, *sh;

    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        p = line;
        if (!field(&p, &n) || !field(&p, &salt) || !field(&p, &hash) ||
            !field(&p, &u) || !field(&p, &g) || !field(&p, &dir))
            continue;
        sh = p;
        if ((name && !strcmp(n, name)) || (!name && atoi(u) == id)) {
            fclose(f);
            pw.pw_name = n;
            pw.pw_uid = atoi(u);
            pw.pw_gid = atoi(g);
            pw.pw_dir = dir;
            pw.pw_shell = sh;
            return &pw;
        }
    }
    fclose(f);
    return 0;
}

struct passwd *getpwnam(const char *name)
{
    return pwscan(name, 0);
}

struct passwd *getpwuid(uid_t uid)
{
    return pwscan(0, (int)uid);
}

static struct group *grscan(const char *name, int id)
{
    static struct group gr;
    static char line[512];
    static char *mem[1];
    FILE *f = fopen("/etc/group", "r");
    char *p, *n, *pw, *g;

    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        p = line;
        if (!field(&p, &n) || !field(&p, &pw))
            continue;
        g = p;
        p = strchr(g, ':');
        if (p)
            *p = 0;
        if ((name && !strcmp(n, name)) || (!name && atoi(g) == id)) {
            fclose(f);
            gr.gr_name = n;
            gr.gr_passwd = pw;
            gr.gr_gid = atoi(g);
            mem[0] = 0;
            gr.gr_mem = mem;
            return &gr;
        }
    }
    fclose(f);
    return 0;
}

struct group *getgrnam(const char *name)
{
    return grscan(name, 0);
}

struct group *getgrgid(gid_t gid)
{
    return grscan(0, (int)gid);
}

#include <sys/resource.h>

int getpriority(int which, int who)
{
    return 0;
}

int setpriority(int which, int who, int prio)
{
    errno = ENOSYS;
    return -1;
}

int symlinkat(const char *target, int dirfd, const char *path)
{
    errno = ENOSYS;
    return -1;
}

int linkat(int fd1, const char *p1, int fd2, const char *p2, int flags)
{
    errno = ENOSYS;
    return -1;
}
