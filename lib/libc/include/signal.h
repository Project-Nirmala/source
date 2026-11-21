#ifndef _LIBC_SIGNAL_H
#define _LIBC_SIGNAL_H

#include <sys/types.h>

#define SIGHUP  1
#define SIGINT  2
#define SIGQUIT 3
#define SIGKILL 9
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGWINCH 28

typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#define SIG_HOLD ((sighandler_t)2)

int          kill(pid_t pid, int sig);
sighandler_t signal(int sig, sighandler_t handler);

#endif
