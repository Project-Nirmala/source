#ifndef _LIBC_DIRENT_H
#define _LIBC_DIRENT_H

#include <stddef.h>

#define DT_FIFO 1
#define DT_CHR  2
#define DT_BLK  6
#define DT_DIR  4
#define DT_REG  8

struct dirent {
    unsigned long  d_ino;
    long           d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[256];
};

long getdents64(int fd, void *buf, size_t n);

typedef struct __dirstream {
    int fd;
    long pos, len;
    char buf[2048];
} DIR;

DIR *fdopendir(int fd);
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);

#endif
