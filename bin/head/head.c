#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

static int do_fd(int fd, long lines)
{
    char buf[512];
    long n;

    while (lines > 0 && (n = read(fd, buf, sizeof(buf))) > 0) {
        long i;

        for (i = 0; i < n && lines > 0; i++)
            if (buf[i] == '\n')
                lines--;
        write(1, buf, (size_t)i);
    }
    return 0;
}

int main(int argc, char **argv)
{
    long lines = 10;
    int i = 1, rc = 0, files, multi, first = 1;

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
            dprintf(2, "Usage: head [-n N] [FILE]...\n");
            return 1;
        }
    }
    if (i >= argc)
        return do_fd(0, lines);
    files = argc - i;
    multi = files > 1;
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0) {
            dprintf(2, "head: can't open '%s': %s\n", argv[i], strerror(errno));
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
