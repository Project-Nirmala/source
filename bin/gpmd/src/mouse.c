#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include "gpmd.h"

#define BATCH 32

static int fd = -1;
static struct mouse_event queue[BATCH];
static int qlen, qpos;

int mouse_open(const char *path)
{
    fd = open(path, O_RDONLY);
    return fd;
}

int mouse_next(struct mouse_event *ev)
{
    if (qpos == qlen) {
        long n = read(fd, queue, sizeof(queue));

        if (n < 0 && errno == EINTR)
            return -1;
        if (n < (long)sizeof(queue[0]))
            return 0;
        qlen = (int)(n / (long)sizeof(queue[0]));
        qpos = 0;
    }
    *ev = queue[qpos++];
    return 1;
}

#define POLL_US 8000

static unsigned last_ax = ~0u, last_ay = ~0u;
static unsigned char held;

int mouse_absolute(void)
{
    struct mouse_abs a;

    return ioctl(fd, NIR_MOUSEABS, &a) == 0 && a.valid;
}

int mouse_next_abs(struct mouse_event *ev, unsigned *ax, unsigned *ay, int *have_abs)
{
    struct mouse_abs a;
    int pending = 0, r;

    for (;;) {
        if (ioctl(fd, NIR_MOUSEABS, &a) == 0 && a.valid && (a.x != last_ax || a.y != last_ay)) {
            last_ax = a.x;
            last_ay = a.y;
            *ax = a.x;
            *ay = a.y;
            *have_abs = 1;
            memset(ev, 0, sizeof(*ev));
            ev->buttons = held;
            return 1;
        }
        if (qpos < qlen || (ioctl(fd, FIONREAD, &pending) == 0 && pending > 0)) {
            *have_abs = 0;
            r = mouse_next(ev);
            if (r == 1)
                held = ev->buttons;
            return r;
        }
        errno = 0;
        usleep(POLL_US);
        if (errno == EINTR)
            return -1;
    }
}
