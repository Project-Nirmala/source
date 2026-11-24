#ifndef _LIBC_FCNTL_H
#define _LIBC_FCNTL_H

#include <sys/types.h>
#include <sys/fcntl.h>

int open(const char *path, int flags, ...);

int openat(int dirfd, const char *path, int flags, ...);
int creat(const char *path, mode_t mode);
int fcntl(int fd, int cmd, ...);

#endif
