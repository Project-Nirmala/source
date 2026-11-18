#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

int main(int argc, char **argv)
{
    int sig = SIGTERM, i = 1, rc = 0;

    if (i < argc && argv[i][0] == '-') {
        sig = atoi(argv[i] + 1);
        i++;
    }
    if (i >= argc) {
        dprintf(2, "Usage: kill [-SIG] PID...\n");
        return 1;
    }
    for (; i < argc; i++) {
        if (kill(atoi(argv[i]), sig) < 0) {
            dprintf(2, "kill: can't kill pid %s: %s\n", argv[i], strerror(errno));
            rc = 1;
        }
    }
    return rc;
}
