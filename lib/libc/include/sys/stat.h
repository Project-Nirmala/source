#ifndef _LIBC_SYS_STAT_H
#define _LIBC_SYS_STAT_H

#include <sys/types.h>
#include <time.h>

struct stat {
    unsigned long st_dev, st_ino, st_nlink;
    unsigned int  st_mode, st_uid, st_gid, __pad0;
    unsigned long st_rdev;
    long          st_size, st_blksize, st_blocks;
    struct timespec st_atim, st_mtim, st_ctim;
    long          __unused[3];
};

#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFREG  0100000
#define S_IFIFO  0010000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)

int stat(const char *path, struct stat *st);
int lstat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec

int mkdir(const char *path, mode_t mode);
int chmod(const char *path, mode_t mode);
mode_t umask(mode_t mask);

#endif
