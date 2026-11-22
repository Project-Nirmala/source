#ifndef _LIBC_UNISTD_H
#define _LIBC_UNISTD_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/fcntl.h>

ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int     close(int fd);
off_t   lseek(int fd, off_t off, int whence);
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

pid_t   getpid(void);
pid_t   getppid(void);
pid_t   fork(void);
int     pipe(int fds[2]);
int     dup(int fd);
int     dup2(int oldfd, int newfd);
int     chdir(const char *path);
char   *getcwd(char *buf, size_t size);
int     sched_yield(void);
int     usleep(unsigned usec);
int     isatty(int fd);
int     execve(const char *path, char *const argv[], char *const envp[]);
void    _exit(int status) __attribute__((noreturn));
int     unlink(const char *path);
int     execv(const char *path, char *const argv[]);
int     execvp(const char *file, char *const argv[]);
int     execl(const char *path, const char *arg0, ...);
void   *sbrk(long inc);
int     rmdir(const char *path);
int     rename(const char *from, const char *to);
int     truncate(const char *path, off_t len);
int     ftruncate(int fd, off_t len);
int     access(const char *path, int mode);
void    sync(void);
uid_t   getuid(void);
uid_t   geteuid(void);
gid_t   getgid(void);
gid_t   getegid(void);
int     setuid(uid_t uid);
int     setgid(gid_t gid);
int     chown(const char *path, uid_t uid, gid_t gid);
int     gethostname(char *name, size_t size);
int     sethostname(const char *name, size_t size);
int     setkeymap(const char *name);

extern char *optarg;
extern int optind, opterr, optopt;
int getopt(int argc, char *const argv[], const char *opts);
pid_t getpgrp(void);
int setpgid(pid_t pid, pid_t pgid);
pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t pgrp);

#define vfork fork

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

int getgroups(int n, gid_t *list);

#endif
