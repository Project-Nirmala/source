#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

#define KEEP (128 * 1024)

static char buf[KEEP];

static int do_fd(int fd, long lines)
{
    size_t len = 0, p;
    long n, seen = 0;

    while ((n = read(fd, buf + len, KEEP - len)) > 0) {
        len += (size_t)n;
        if (len == KEEP) {
            memmove(buf, buf + KEEP / 2, KEEP / 2);
            len = KEEP / 2;
        }
    }
    if (lines <= 0)
        return 0;
    p = len;
    if (p > 0 && buf[p - 1] == '\n')
        p--;
    while (p > 0) {
        if (buf[p - 1] == '\n' && ++seen == lines)
            break;
        p--;
    }
    write(1, buf + p, len - p);
    return 0;
}

int main(int argc, char **argv)
{
    long lines = 10;
    int i = 1, rc = 0, multi, first = 1;

    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            lines = atoi(argv[i + 1]);
            i += 2;
        } else if (!strncmp(argv[i], "-n", 2) && argv[i][2] >= '0' && argv[i][2] <= '9') {
            lines = atoi(argv[i] + 2);
            i++;
        } else if (argv[i][1] >= '0' && argv[i][1] <= '9') {
            lines = atoi(argv[i] + 1);
            i++;
        } else {
            dprintf(2, "Usage: tail [-n N] [FILE]...\n");
            return 1;
        }
    }
    if (i >= argc)
        return do_fd(0, lines);
    multi = argc - i > 1;
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0) {
            dprintf(2, "tail: can't open '%s': %s\n", argv[i], strerror(errno));
            rc = 1;
            continue;
        }
        if (multi)
            printf("%s==> %s <==\n", first ? "" : "\n", argv[i]);
        first = 0;
        do_fd(fd, lines);
        close(fd);
    }
    return rc;
}
