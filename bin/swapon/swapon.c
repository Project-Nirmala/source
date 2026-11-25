#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/swap.h>
#include <sys/sysinfo.h>

#define SWAP_CONF "/etc/swap"

static int conf_spec(char *out, size_t size)
{
    long n;
    int fd = open(SWAP_CONF, O_RDONLY), i;

    if (fd < 0)
        return -1;
    n = read(fd, out, size - 1);
    close(fd);
    if (n <= 0)
        return -1;
    out[n] = 0;
    for (i = 0; out[i] && out[i] != '\n'; i++)
        ;
    out[i] = 0;
    return i ? 0 : -1;
}

static int summary(void)
{
    struct sysinfo si;

    if (sysinfo(&si) < 0) {
        fprintf(stderr, "swapon: %s\n", strerror(errno));
        return 1;
    }
    if (!si.totalswap) {
        printf("no swap in use\n");
        return 0;
    }
    printf("swap: %lu KiB total, %lu KiB used, %lu KiB free\n", si.totalswap >> 10,
           (si.totalswap - si.freeswap) >> 10, si.freeswap >> 10);
    return 0;
}

static int apply(const char *prog, int off, const char *spec)
{
    int r = off ? swapoff(spec) : swapon(spec, 0);

    if (r < 0) {
        fprintf(stderr, "%s: %s: %s\n", prog, spec, strerror(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *prog = strrchr(argv[0], '/');
    char spec[96];
    int off, all = 0, show = 0, rc = 0, i, n = 0;

    prog = prog ? prog + 1 : argv[0];
    off = !strcmp(prog, "swapoff");
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-a"))
            all = 1;
        else if (!strcmp(argv[i], "-s"))
            show = 1;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "usage: %s [-a] [-s] [device]\n", prog);
            return 1;
        }
    }
    if (show || (argc == 1 && !off))
        return summary();
    if (all) {
        if (conf_spec(spec, sizeof(spec)) < 0)
            return 0;
        return apply(prog, off, spec);
    }
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-')
            continue;
        rc |= apply(prog, off, argv[i]);
        n++;
    }
    if (!n) {
        fprintf(stderr, "usage: %s [-a] [-s] [device]\n", prog);
        return 1;
    }
    return rc;
}
