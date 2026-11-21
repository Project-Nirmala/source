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
