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
int     unlinkat(int dirfd, const char *path, int flags);
int     lchown(const char *path, uid_t uid, gid_t gid);
ssize_t readlink(const char *path, char *buf, size_t n);
int     symlink(const char *target, const char *path);
unsigned sleep(unsigned sec);
int     link(const char *from, const char *to);
int     chroot(const char *path);
char   *getlogin(void);
char   *ttyname(int fd);
int     fchown(int fd, uid_t uid, gid_t gid);
int     fchownat(int dirfd, const char *path, uid_t uid, gid_t gid, int flags);
int     faccessat(int dirfd, const char *path, int mode, int flags);
int     execlp(const char *file, const char *arg0, ...);
#define _SC_ARG_MAX 0
#define _SC_LOGIN_NAME_MAX 71
long    sysconf(int name);
int     symlinkat(const char *target, int dirfd, const char *path);
int     linkat(int fd1, const char *p1, int fd2, const char *p2, int flags);
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

int fchdir(int fd);
pid_t setsid(void);
int killpg(pid_t pgrp, int sig);
ssize_t pread(int fd, void *buf, size_t n, off_t off);
ssize_t pwrite(int fd, const void *buf, size_t n, off_t off);

#endif
