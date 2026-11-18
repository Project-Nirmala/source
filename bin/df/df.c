#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/statfs.h>

int main(int argc, char **argv)
{
    struct statfs s;
    const char *path = argc > 1 ? argv[1] : "/";
    unsigned long total, used, avail, pct;

    if (statfs(path, &s) < 0) {
        dprintf(2, "df: %s: %s\n", path, strerror(errno));
        return 1;
    }
    total = s.f_blocks * s.f_bsize / 1024;
    avail = s.f_bfree * s.f_bsize / 1024;
    used = total - avail;
    pct = total ? (used * 100 + total - 1) / total : 0;
    printf("Filesystem           1K-blocks      Used Available Use%% Mounted on\n");
    printf("%-20s %9lu %9lu %9lu %3lu%% %s\n", "lemonfs", total, used, avail, pct, path);
    return 0;
}
