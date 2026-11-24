#ifndef _LIBC_SYS_TIME_H
#define _LIBC_SYS_TIME_H

#include <time.h>

int utimes(const char *path, const struct timeval tv[2]);

#endif
