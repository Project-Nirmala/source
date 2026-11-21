#ifndef _LIBC_POLL_H
#define _LIBC_POLL_H

struct pollfd {
    int   fd;
    short events;
    short revents;
};

#define POLLIN  0x001
#define POLLOUT 0x004
#define POLLERR 0x008
#define POLLHUP 0x010

typedef unsigned long nfds_t;

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#endif
