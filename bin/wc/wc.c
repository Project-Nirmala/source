#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>

static int show_l, show_w, show_c;
static unsigned long tl, tw, tc;

static void print_counts(unsigned long l, unsigned long w, unsigned long c, const char *name)
{
    int width = (show_l + show_w + show_c) == 1 ? 0 : 7, first = 1;

    if (show_l) {
        printf("%*lu", width, l);
        first = 0;
    }
    if (show_w) {
        printf("%s%*lu", first ? "" : " ", width, w);
        first = 0;
    }
    if (show_c) {
        printf("%s%*lu", first ? "" : " ", width, c);
        first = 0;
    }
    if (name[0])
        printf(" %s", name);
    printf("\n");
}

static int count_fd(int fd, const char *name)
{
    char buf[1024];
    unsigned long l = 0, w = 0, c = 0;
    int inword = 0;
    long n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        long i;

        c += (unsigned long)n;
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n')
                l++;
            if (isspace((unsigned char)buf[i]))
                inword = 0;
            else if (!inword) {
                inword = 1;
                w++;
            }
        }
    }
    if (n < 0) {
        dprintf(2, "wc: %s: %s\n", name, strerror(errno));
        return 1;
    }
    print_counts(l, w, c, name[0] == '-' && !name[1] ? "" : name);
    tl += l;
    tw += w;
    tc += c;
    return 0;
}

int main(int argc, char **argv)
{
    int i = 1, rc = 0, files = 0;

    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *f;

        for (f = argv[i] + 1; *f; f++) {
            if (*f == 'l')
                show_l = 1;
            else if (*f == 'w')
                show_w = 1;
            else if (*f == 'c')
                show_c = 1;
            else {
                dprintf(2, "Usage: wc [-lwc] [FILE]...\n");
                return 1;
            }
        }
    }
    if (!show_l && !show_w && !show_c)
        show_l = show_w = show_c = 1;
    if (i >= argc)
        return count_fd(0, "-");
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);

        files++;
        if (fd < 0) {
            dprintf(2, "wc: %s: %s\n", argv[i], strerror(errno));
            rc = 1;
            continue;
        }
        rc |= count_fd(fd, argv[i]);
        close(fd);
    }
    if (files > 1)
        print_counts(tl, tw, tc, "total");
    return rc;
}
