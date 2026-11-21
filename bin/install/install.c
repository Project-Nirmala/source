#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/blkio.h>
#include <diskfmt.h>

#define ESP_MIB 100u
#define MIN_ROOT_MIB 64u

static uint64_t device_sectors(int fd)
{
    uint64_t bytes = 0;

    if (ioctl(fd, BLKGETSIZE64, &bytes) < 0 || bytes == 0)
        bytes = (uint64_t)lseek(fd, 0, SEEK_END);
    return bytes / 512;
}

static int readable(const char *path)
{
    int fd = open(path, O_RDONLY);

    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

static void readline(char *buf, size_t size)
{
    size_t n = 0;
    char c;

    while (n + 1 < size && read(0, &c, 1) == 1 && c != '\n')
        buf[n++] = c;
    buf[n] = 0;
}

static int list_disks(char names[][8], int max)
{
    static const char *const prefix[] = { "sd", "vd" };
    int count = 0;
    unsigned p, l;

    for (p = 0; p < 2; p++)
        for (l = 0; l < 8 && count < max; l++) {
            char path[24], model[64];
            uint64_t bytes = 0;
            int fd, root = 0;

            snprintf(names[count], 8, "%s%c", prefix[p], (char)('a' + l));
            snprintf(path, sizeof(path), "/dev/%s", names[count]);
            fd = open(path, O_RDONLY);
            if (fd < 0)
                continue;
            model[0] = 0;
            ioctl(fd, BLKGETSIZE64, &bytes);
            ioctl(fd, NIR_BLKMODEL, model);
            ioctl(fd, NIR_BLKROOT, &root);
            close(fd);
            printf("  %d) %-5s %8lu MiB  %s%s\n", count + 1, names[count], (unsigned long)(bytes >> 20), model,
                   root ? "  (in use, cannot be installed to)" : "");
            count++;
        }
    return count;
}

static int zero_range(int fd, uint64_t first_sector, uint64_t bytes)
{
    static char z[65536];

    if (lseek(fd, (off_t)(first_sector * 512), SEEK_SET) < 0)
        return -1;
    while (bytes) {
        size_t n = bytes > sizeof(z) ? sizeof(z) : (size_t)bytes;

        if (write(fd, z, n) != (long)n)
            return -1;
        bytes -= n;
    }
    return 0;
}

static int install_to(const char *name)
{
    char path[24], guid[37], conf[512];
    struct gpt_part p[2];
    struct fat32 *fs;
    uint64_t total, esp_sectors = (uint64_t)ESP_MIB * 2048, esp_first = 2048;
    int fd, src, root = 0;

    snprintf(path, sizeof(path), "/dev/%s", name);
    fd = open(path, O_RDWR);
    if (fd < 0) {
        dprintf(2, "install: cannot open %s\n", path);
        return 1;
    }
    if (ioctl(fd, NIR_BLKROOT, &root) == 0 && root) {
        dprintf(2, "install: %s holds the running system\n", path);
        return 1;
    }
    total = device_sectors(fd);
    if (total < esp_first + esp_sectors + (uint64_t)MIN_ROOT_MIB * 2048 + 34) {
        dprintf(2, "install: disk is too small (needs at least %u MiB)\n", ESP_MIB + MIN_ROOT_MIB + 2);
        return 1;
    }

    printf("Partitioning %s\n", path);
    memset(p, 0, sizeof(p));
    gpt_guid_parse(GPT_TYPE_ESP, p[0].type);
    gpt_random_guid(p[0].guid);
    p[0].first = esp_first;
    p[0].last = esp_first + esp_sectors - 1;
    strcpy(p[0].name, "EFI System");
    gpt_guid_parse(GPT_TYPE_LEMON, p[1].type);
    gpt_random_guid(p[1].guid);
    p[1].first = p[0].last + 1;
    p[1].last = total - 34;
    strcpy(p[1].name, "nirmala-root");
    if (gpt_write(fd, total, p, 2) < 0 || zero_range(fd, p[1].first, 1u << 20) < 0) {
        dprintf(2, "install: cannot write the partition table\n");
        return 1;
    }

    printf("Creating the boot partition\n");
    if (fat32_format(fd, p[0].first, esp_sectors, "NIRMALAESP") < 0) {
        dprintf(2, "install: cannot format the boot partition\n");
        return 1;
    }
    fs = fat32_open(fd, p[0].first, esp_sectors, 1);
    if (!fs) {
        dprintf(2, "install: cannot open the boot partition\n");
        return 1;
    }
    gpt_guid_format(p[1].guid, guid);
    snprintf(conf, sizeof(conf),
             "timeout: 3\nserial: yes\n\n/NirmalaSYS\n    protocol: limine\n    path: boot():/boot/nirmala\n"
             "    module_path: boot():/boot/initrd.tar\n    cmdline: root=PARTUUID=%s\n", guid);

    printf("Installing the boot loader\n");
    src = open("/usr/share/limine/BOOTX64.EFI", O_RDONLY);
    if (src < 0 || fat32_put_fd(fs, "/EFI/BOOT/BOOTX64.EFI", src) < 0) {
        dprintf(2, "install: cannot copy the boot loader\n");
        return 1;
    }
    close(src);
    if (fat32_put_mem(fs, "/limine.conf", conf, strlen(conf)) < 0) {
        dprintf(2, "install: cannot write the boot configuration\n");
        return 1;
    }

    printf("Installing the system\n");
    src = open("/dev/kernel", O_RDONLY);
    if (src < 0 || fat32_put_fd(fs, "/boot/nirmala", src) < 0) {
        dprintf(2, "install: cannot copy the kernel\n");
        return 1;
    }
    close(src);
    src = open("/dev/initrd", O_RDONLY);
    if (src < 0 || fat32_put_fd(fs, "/boot/initrd.tar", src) < 0) {
        dprintf(2, "install: cannot copy the system image\n");
        return 1;
    }
    close(src);
    if (fat32_close(fs) < 0) {
        dprintf(2, "install: cannot finish writing the boot partition\n");
        return 1;
    }
    sync();
    ioctl(fd, BLKRRPART, 0);
    close(fd);
    printf("Installation finished. Boot this disk in UEFI mode.\n");
    return 0;
}

int main(int argc, char **argv)
{
    char names[16][8], answer[64], choice[16];
    const char *target = argc > 1 ? argv[1] : NULL;
    int count, r;

    if (!readable("/dev/kernel") || !readable("/usr/share/limine/BOOTX64.EFI")) {
        dprintf(2, "install: this system image cannot install itself\n");
        return 1;
    }
    printf("NirmalaSYS installer\n\n");
    count = list_disks(names, 16);
    if (count == 0) {
        dprintf(2, "install: no disks found\n");
        return 1;
    }
    if (!target) {
        printf("\nInstall to which disk (number or name)? ");
        readline(answer, sizeof(answer));
        if (answer[0] >= '1' && answer[0] <= '9' && atoi(answer) <= count)
            snprintf(choice, sizeof(choice), "%s", names[atoi(answer) - 1]);
        else
            snprintf(choice, sizeof(choice), "%s", answer);
        target = choice;
    }
    printf("\nAll data on %s will be erased. Type yes to continue: ", target);
    readline(answer, sizeof(answer));
    if (strcmp(answer, "yes")) {
        printf("Cancelled.\n");
        return 1;
    }
    r = install_to(target);
    if (getpid() == 1) {
        static char *const init_argv[] = { "init", NULL };

        printf("Press Enter to continue to the live system.\n");
        readline(answer, sizeof(answer));
        execv("/sbin/init", init_argv);
    }
    return r;
}
