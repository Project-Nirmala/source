#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <utime.h>

int main(int argc, char **argv)
{
    int i, rc = 0;

    if (argc < 2) {
        dprintf(2, "Usage: touch FILE...\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT, 0644);

        if (fd < 0) {
            dprintf(2, "touch: %s: %s\n", argv[i], strerror(errno));
            rc = 1;
        } else {
            close(fd);
            if (utime(argv[i], 0) < 0) {
                dprintf(2, "touch: %s: %s\n", argv[i], strerror(errno));
                rc = 1;
            }
        }
    }
    return rc;
}
