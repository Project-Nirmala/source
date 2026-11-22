#ifndef _LIBC_FCNTL_H
#define _LIBC_FCNTL_H

#include <sys/fcntl.h>

int open(const char *path, int flags, ...);

int fcntl(int fd, int cmd, ...);

#endif
