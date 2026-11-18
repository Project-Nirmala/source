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

#endif
