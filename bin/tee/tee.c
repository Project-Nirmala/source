#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int fds[8], nf = 0, i = 1, flags = O_WRONLY | O_CREAT | O_TRUNC, rc = 0;
    char buf[512];
    long n;

    if (i < argc && !strcmp(argv[i], "-a")) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
        i++;
    }
    for (; i < argc && nf < 8; i++) {
        int fd = open(argv[i], flags, 0644);

        if (fd < 0) {
            dprintf(2, "tee: %s: %s\n", argv[i], strerror(errno));
            rc = 1;
        } else {
            fds[nf++] = fd;
        }
    }
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        int k;

        write(1, buf, (size_t)n);
        for (k = 0; k < nf; k++)
            write(fds[k], buf, (size_t)n);
    }
    for (i = 0; i < nf; i++)
        close(fds[i]);
    return rc;
}
