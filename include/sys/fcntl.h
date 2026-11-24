#ifndef _SYS_FCNTL_H
#define _SYS_FCNTL_H

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_ACCMODE 3
#define O_CREAT     0100
#define O_EXCL      0200
#define O_TRUNC     01000
#define O_APPEND    02000
#define O_DIRECTORY 0200000

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define O_NONBLOCK 04000
#define O_CLOEXEC  02000000
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1
#define F_DUPFD_CLOEXEC 1030

#define AT_FDCWD -100
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define AT_EACCESS 0x200
#define AT_SYMLINK_FOLLOW 0x400

#endif
