#ifndef _LIBC_ERRNO_H
#define _LIBC_ERRNO_H

#include <sys/errno.h>

extern int errno;

#ifndef EWOULDBLOCK
#define EWOULDBLOCK 11
#endif
#ifndef ELOOP
#define ELOOP 40
#endif

#endif
