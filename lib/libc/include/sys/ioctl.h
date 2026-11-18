#ifndef _LIBC_SYS_IOCTL_H
#define _LIBC_SYS_IOCTL_H

#include <sys/termios.h>

int ioctl(int fd, unsigned long req, ...);

#endif
