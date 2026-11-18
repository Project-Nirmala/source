#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "nirmake.h"

#define MAX_TRACK 256

typedef struct Track {
    pid_t pid;
    char *target;
    int existed;
    NmTime before;
} Track;

static Track tracked[MAX_TRACK];

pid_t job_spawn(const char *cmd, int ignore_err)
{
    pid_t pid;
    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        execl("/bin/sh", "sh", ignore_err ? "-c" : "-ec", cmd, (char *)NULL);
        _exit(127);
    }
    return pid;
}

pid_t job_wait(int *status)
{
    pid_t pid;
    for (;;) {
        pid = waitpid(-1, status, 0);
        if (pid < 0 && errno == EINTR)
            continue;
        return pid;
    }
}

void job_track(pid_t pid, const char *target, int existed, NmTime before)
{
    int i;
    for (i = 0; i < MAX_TRACK; i++) {
        if (tracked[i].pid == 0) {
            tracked[i].target = target ? nm_strdup(target) : NULL;
            tracked[i].existed = existed;
            tracked[i].before = before;
            tracked[i].pid = pid;
            return;
        }
    }
}

void job_untrack(pid_t pid)
{
    int i;
    for (i = 0; i < MAX_TRACK; i++) {
        if (tracked[i].pid == pid) {
            free(tracked[i].target);
            tracked[i].target = NULL;
            tracked[i].pid = 0;
            return;
        }
    }
}

static void on_signal(int sig)
{
    int i;
    for (i = 0; i < MAX_TRACK; i++) {
        struct stat st;
        if (tracked[i].pid == 0)
            continue;
        kill(tracked[i].pid, sig);
        if (tracked[i].target != NULL && stat(tracked[i].target, &st) == 0) {
            int changed = !tracked[i].existed || (st.st_mtime != tracked[i].before.s);
            if (changed && S_ISREG(st.st_mode))
                unlink(tracked[i].target);
        }
    }
    signal(sig, SIG_DFL);
    kill(getpid(), sig);
}

void job_install_signals(void)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGQUIT, on_signal);
}
