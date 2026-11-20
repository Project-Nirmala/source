#ifndef _COMPAT_POLL_H
#define _COMPAT_POLL_H
struct pollfd {
    int   fd;
    short events;
    short revents;
};
#define POLLIN 0x001
int poll(struct pollfd *fds, unsigned long nfds, int timeout);
#endif
