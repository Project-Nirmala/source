#ifndef _LIBC_SYS_STATFS_H
#define _LIBC_SYS_STATFS_H

struct statfs {
    long          f_type;
    unsigned long f_bsize, f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    int           f_fsid[2];
    long          f_namelen, f_frsize, f_flags, f_spare[4];
};

int statfs(const char *path, struct statfs *buf);

#endif
