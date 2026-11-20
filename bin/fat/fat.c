#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/blkio.h>
#include <diskfmt.h>

static uint64_t device_sectors(int fd)
{
    uint64_t bytes = 0;

    if (ioctl(fd, BLKGETSIZE64, &bytes) < 0 || bytes == 0)
        bytes = (uint64_t)lseek(fd, 0, SEEK_END);
    return bytes / 512;
}

static void ls_line(const char *name, uint32_t size, int isdir, void *ctx)
{
    (void)ctx;
    if (isdir)
        printf("%10s  %s/\n", "<DIR>", name);
    else
        printf("%10lu  %s\n", (unsigned long)size, name);
}

static void usage(void)
{
    dprintf(2, "usage: fat DEVICE mkfs [label]\n"
               "       fat DEVICE ls [path]\n"
               "       fat DEVICE mkdir path\n"
               "       fat DEVICE put source dest\n"
               "       fat DEVICE cat path\n"
               "       fat DEVICE rm path\n");
}

int main(int argc, char **argv)
{
    int fd, r = 1, writable;
    uint64_t nsect;
    struct fat32 *fs;
    const char *cmd;

    if (argc < 3) {
        usage();
        return 1;
    }
    cmd = argv[2];
    writable = strcmp(cmd, "ls") && strcmp(cmd, "cat");
    fd = open(argv[1], writable ? O_RDWR : O_RDONLY);
    if (fd < 0) {
        dprintf(2, "fat: cannot open %s\n", argv[1]);
        return 1;
    }
    nsect = device_sectors(fd);
    if (!strcmp(cmd, "mkfs")) {
        if (fat32_format(fd, 0, nsect, argc > 3 ? argv[3] : "NIRMALA") < 0) {
            dprintf(2, "fat: cannot format (partition needs at least 34 MiB)\n");
            return 1;
        }
        sync();
        return 0;
    }
    fs = fat32_open(fd, 0, nsect, writable);
    if (!fs) {
        dprintf(2, "fat: %s does not hold a FAT32 filesystem\n", argv[1]);
        return 1;
    }
    if (!strcmp(cmd, "ls"))
        r = fat32_ls(fs, argc > 3 ? argv[3] : "/", ls_line, NULL) < 0;
    else if (!strcmp(cmd, "mkdir") && argc > 3)
        r = fat32_mkdir(fs, argv[3]) < 0;
    else if (!strcmp(cmd, "cat") && argc > 3)
        r = fat32_cat(fs, argv[3], 1) < 0;
    else if (!strcmp(cmd, "rm") && argc > 3)
        r = fat32_remove(fs, argv[3]) < 0;
    else if (!strcmp(cmd, "put") && argc > 4) {
        int src = open(argv[3], O_RDONLY);

        if (src < 0)
            dprintf(2, "fat: cannot open %s\n", argv[3]);
        else {
            r = fat32_put_fd(fs, argv[4], src) < 0;
            close(src);
        }
    } else
        usage();
    if (fat32_close(fs) < 0)
        r = 1;
    sync();
    if (r)
        dprintf(2, "fat: %s failed\n", cmd);
    return r;
}
