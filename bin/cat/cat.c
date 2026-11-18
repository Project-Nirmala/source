#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static const char *errstr(int e)
{
    switch (e) {
    case ENOENT: return "No such file or directory";
    case EISDIR: return "Is a directory";
    case EACCES: return "Permission denied";
    case EINTR:  return "Interrupted";
    default:     return "I/O error";
    }
}

static int copy(int fd, const char *name)
{
    char buf[512];
    long n;

    (void)name;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(1, buf, (size_t)n) < 0)
            return 1;
    }
    if (n < 0) {
        dprintf(2, "cat: read error: %s\n", errstr(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i, rc = 0, fd;

    if (argc < 2)
        return copy(0, "stdin");
    for (i = 1; i < argc; i++) {
        fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            dprintf(2, "cat: can't open '%s': %s\n", argv[i], errstr(errno));
            rc = 1;
            continue;
        }
        rc |= copy(fd, argv[i]);
        close(fd);
    }
    return rc;
}
