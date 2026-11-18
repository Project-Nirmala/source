#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>

#define LINEMAX 4096

static int opt_i, opt_v, opt_n, opt_c, multi;
static const char *pat;
static size_t patlen;

static int matches(const char *line)
{
    size_t n = strlen(line), i, j;

    if (patlen == 0)
        return 1;
    for (i = 0; i + patlen <= n; i++) {
        for (j = 0; j < patlen; j++) {
            char a = line[i + j], b = pat[j];

            if (opt_i) {
                a = (char)tolower((unsigned char)a);
                b = (char)tolower((unsigned char)b);
            }
            if (a != b)
                break;
        }
        if (j == patlen)
            return 1;
    }
    return 0;
}

static int do_fd(int fd, const char *name)
{
    char line[LINEMAX], buf[512];
    size_t n = 0;
    long r, lineno = 0, count = 0;
    int found = 0;

    for (;;) {
        long i;

        r = read(fd, buf, sizeof(buf));
        if (r <= 0 && n == 0)
            break;
        if (r <= 0) {
            buf[0] = '\n';
            r = 1;
        }
        for (i = 0; i < r; i++) {
            if (buf[i] != '\n') {
                if (n < LINEMAX - 1)
                    line[n++] = buf[i];
                continue;
            }
            line[n] = 0;
            n = 0;
            lineno++;
            if (matches(line) != opt_v) {
                found = 1;
                count++;
                if (!opt_c) {
                    if (multi)
                        printf("%s:", name);
                    if (opt_n)
                        printf("%ld:", lineno);
                    printf("%s\n", line);
                }
            }
        }
    }
    if (opt_c) {
        if (multi)
            printf("%s:", name);
        printf("%ld\n", count);
    }
    return found;
}

int main(int argc, char **argv)
{
    int i = 1, found = 0, errors = 0;

    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *f;

        for (f = argv[i] + 1; *f; f++) {
            if (*f == 'i')
                opt_i = 1;
            else if (*f == 'v')
                opt_v = 1;
            else if (*f == 'n')
                opt_n = 1;
            else if (*f == 'c')
                opt_c = 1;
            else {
                dprintf(2, "Usage: grep [-ivnc] PATTERN [FILE]...\n");
                return 2;
            }
        }
    }
    if (i >= argc) {
        dprintf(2, "Usage: grep [-ivnc] PATTERN [FILE]...\n");
        return 2;
    }
    pat = argv[i++];
    patlen = strlen(pat);
    if (i >= argc)
        return do_fd(0, "(stdin)") ? 0 : 1;
    multi = argc - i > 1;
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0) {
            dprintf(2, "grep: %s: %s\n", argv[i], strerror(errno));
            errors = 1;
            continue;
        }
        found |= do_fd(fd, argv[i]);
        close(fd);
    }
    return errors ? 2 : found ? 0 : 1;
}
