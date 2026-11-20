#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/blkio.h>

static int show(const char *name)
{
    char path[32], model[64];
    unsigned long long size = 0, start = 0;
    int fd, root = 0;

    snprintf(path, sizeof(path), "/dev/%s", name);
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    model[0] = 0;
    ioctl(fd, BLKGETSIZE64, &size);
    ioctl(fd, NIR_BLKSTART, &start);
    ioctl(fd, NIR_BLKMODEL, model);
    ioctl(fd, NIR_BLKROOT, &root);
    printf("%-8s %10llu %12llu %-5s %s\n", name, size / (1024 * 1024), start, root ? "root" : "-",
           isdigit((unsigned char)name[strlen(name) - 1]) ? "" : model);
    close(fd);
    return 1;
}

int main(void)
{
    static const char *const prefix[] = { "sd", "vd" };
    unsigned p, l, n;
    char name[8];

    printf("%-8s %10s %12s %-5s %s\n", "NAME", "SIZE(MiB)", "START(LBA)", "ROOT", "MODEL");
    for (p = 0; p < 2; p++)
        for (l = 0; l < 8; l++) {
            snprintf(name, sizeof(name), "%s%c", prefix[p], (char)('a' + l));
            if (!show(name))
                continue;
            for (n = 1; n <= 16; n++) {
                snprintf(name, sizeof(name), "%s%c%u", prefix[p], (char)('a' + l), n);
                show(name);
            }
        }
    return 0;
}
