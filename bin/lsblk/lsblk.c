#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/blkio.h>

#define LETTER_DISKS 8
#define NVME_CTRLS 4
#define NVME_NS 4
#define MAX_PARTS 16

static int show(const char *name, int whole)
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
    printf("%-12s %10llu %12llu %-5s %s\n", name, size / (1024 * 1024), start, root ? "root" : "-",
           whole ? model : "");
    close(fd);
    return 1;
}

static void show_parts(const char *disk, const char *sep)
{
    char name[32];
    unsigned n;

    for (n = 1; n <= MAX_PARTS; n++) {
        snprintf(name, sizeof(name), "%s%s%u", disk, sep, n);
        show(name, 0);
    }
}

int main(void)
{
    static const char *const prefix[] = { "sd", "vd" };
    unsigned p, l, c, n;
    char name[32];

    printf("%-12s %10s %12s %-5s %s\n", "NAME", "SIZE(MiB)", "START(LBA)", "ROOT", "MODEL");
    for (p = 0; p < 2; p++)
        for (l = 0; l < LETTER_DISKS; l++) {
            snprintf(name, sizeof(name), "%s%c", prefix[p], (char)('a' + l));
            if (show(name, 1))
                show_parts(name, "");
        }
    for (c = 0; c < NVME_CTRLS; c++)
        for (n = 1; n <= NVME_NS; n++) {
            snprintf(name, sizeof(name), "nvme%un%u", c, n);
            if (show(name, 1))
                show_parts(name, "p");
        }
    return 0;
}
