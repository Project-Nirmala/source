#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

#define LINEMAX 2048

static int count_mode;
static char prev[LINEMAX], cur[LINEMAX];
static int have_prev;
static unsigned long run;

static void flush_prev(void)
{
    if (!have_prev)
        return;
    if (count_mode)
        printf("%7lu ", run);
    printf("%s\n", prev);
}

static void feed(const char *line)
{
    if (have_prev && !strcmp(line, prev)) {
        run++;
        return;
    }
    flush_prev();
    strncpy(prev, line, LINEMAX - 1);
    prev[LINEMAX - 1] = 0;
    have_prev = 1;
    run = 1;
}

static int do_fd(int fd)
{
    char buf[512];
    size_t n = 0;
    long r;

    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        long i;

        for (i = 0; i < r; i++) {
            if (buf[i] == '\n') {
                cur[n] = 0;
                feed(cur);
                n = 0;
            } else if (n < LINEMAX - 1) {
                cur[n++] = buf[i];
            }
        }
    }
    if (n) {
        cur[n] = 0;
        feed(cur);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i = 1;

    if (i < argc && !strcmp(argv[i], "-c")) {
        count_mode = 1;
        i++;
    }
    if (i < argc) {
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0) {
            dprintf(2, "uniq: can't open '%s': %s\n", argv[i], strerror(errno));
            return 1;
        }
        do_fd(fd);
        close(fd);
    } else {
        do_fd(0);
    }
    flush_prev();
    return 0;
}
