#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

#define MAXTEXT (128 * 1024)
#define MAXLINES 8192

static char  text[MAXTEXT + 1];
static char *lines[MAXLINES];
static size_t tlen;
static int nlines, reverse, numeric, unique;

static int cmp(const void *a, const void *b)
{
    const char *x = *(char *const *)a, *y = *(char *const *)b;
    int r;

    if (numeric) {
        long nx = strtol(x, 0, 10), ny = strtol(y, 0, 10);

        r = nx < ny ? -1 : nx > ny ? 1 : 0;
        if (r == 0)
            r = strcmp(x, y);
    } else {
        r = strcmp(x, y);
    }
    return reverse ? -r : r;
}

static int slurp(int fd, const char *name)
{
    long n;

    while ((n = read(fd, text + tlen, MAXTEXT - tlen)) > 0) {
        tlen += (size_t)n;
        if (tlen == MAXTEXT) {
            dprintf(2, "sort: out of memory\n");
            return 1;
        }
    }
    if (n < 0) {
        dprintf(2, "sort: read error on '%s': %s\n", name, strerror(errno));
        return 1;
    }
    if (tlen && text[tlen - 1] != '\n')
        text[tlen++] = '\n';
    return 0;
}

int main(int argc, char **argv)
{
    int i = 1, rc = 0, any = 0;
    size_t p;

    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *f;

        for (f = argv[i] + 1; *f; f++) {
            if (*f == 'r')
                reverse = 1;
            else if (*f == 'n')
                numeric = 1;
            else if (*f == 'u')
                unique = 1;
            else {
                dprintf(2, "Usage: sort [-nru] [FILE]...\n");
                return 1;
            }
        }
    }
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);

        any = 1;
        if (fd < 0) {
            dprintf(2, "sort: can't open '%s': %s\n", argv[i], strerror(errno));
            rc = 1;
            continue;
        }
        rc |= slurp(fd, argv[i]);
        close(fd);
    }
    if (!any)
        rc |= slurp(0, "stdin");
    for (p = 0; p < tlen && nlines < MAXLINES;) {
        lines[nlines++] = text + p;
        while (p < tlen && text[p] != '\n')
            p++;
        text[p++] = 0;
    }
    qsort(lines, (size_t)nlines, sizeof(char *), cmp);
    for (i = 0; i < nlines; i++) {
        if (unique && i > 0 && !strcmp(lines[i], lines[i - 1]))
            continue;
        write(1, lines[i], strlen(lines[i]));
        write(1, "\n", 1);
    }
    return rc;
}
