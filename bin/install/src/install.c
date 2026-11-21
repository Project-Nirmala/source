#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/blkio.h>
#include <diskfmt.h>
#include "mkroot.h"

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

static int readline(char *buf, size_t size)
{
    size_t n = 0;
    char c;
    long r = 1;

    while (n + 1 < size && (r = read(0, &c, 1)) == 1 && c != '\n')
        buf[n++] = c;
    buf[n] = 0;
    return r <= 0 && n == 0 ? -1 : (int)n;
}

static int readsecret(char *buf, size_t size)
{
    struct termios t, raw;
    int n;

    tcgetattr(0, &t);
    raw = t;
    raw.c_lflag &= ~(unsigned)ECHO;
    tcsetattr(0, TCSANOW, &raw);
    n = readline(buf, size);
    tcsetattr(0, TCSANOW, &t);
    printf("\n");
    return n;
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

struct layout {
    uint64_t total, esp_first, esp_sectors, root_first, root_sectors;
};

static int probe_disk(const char *name, uint64_t *total, int *in_use)
{
    char path[24];
    int fd, root = 0;

    snprintf(path, sizeof(path), "/dev/%s", name);
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    *total = device_sectors(fd);
    ioctl(fd, NIR_BLKROOT, &root);
    close(fd);
    *in_use = root;
    return 0;
}

static int plan_layout(uint64_t total, struct layout *lay)
{
    lay->total = total;
    lay->esp_first = 2048;
    lay->esp_sectors = (uint64_t)ESP_MIB * 2048;
    lay->root_first = lay->esp_first + lay->esp_sectors;
    if (total < lay->root_first + (uint64_t)MIN_ROOT_MIB * 2048 + 34)
        return -1;
    lay->root_sectors = (total - 34) - lay->root_first + 1;
    return 0;
}

static int valid_username(const char *s)
{
    size_t i, n = strlen(s);

    if (n == 0 || n > 31)
        return 0;
    if (!(s[0] >= 'a' && s[0] <= 'z') && s[0] != '_')
        return 0;
    for (i = 1; i < n; i++)
        if (!(s[i] >= 'a' && s[i] <= 'z') && !(s[i] >= '0' && s[i] <= '9') && s[i] != '_' && s[i] != '-')
            return 0;
    return strcmp(s, "root") != 0;
}

static int ask_password(const char *who, char *out, size_t size)
{
    char again[128];
    int tries;

    for (tries = 0; tries < 3; tries++) {
        printf("Password for %s: ", who);
        if (readsecret(out, size) < 0)
            return -1;
        if (!out[0]) {
            printf("The password cannot be empty.\n");
            continue;
        }
        printf("Retype the password: ");
        if (readsecret(again, sizeof(again)) < 0)
            return -1;
        if (!strcmp(out, again)) {
            memset(again, 0, sizeof(again));
            return 0;
        }
        printf("The passwords do not match.\n");
    }
    memset(again, 0, sizeof(again));
    return -1;
}

static int collect_accounts(struct account *acc)
{
    char name[64];
    int tries;

    memset(acc, 0, sizeof(*acc) * MKROOT_MAX_ACCOUNTS);
    snprintf(acc[0].name, sizeof(acc[0].name), "root");
    snprintf(acc[0].home, sizeof(acc[0].home), "/root");
    snprintf(acc[0].shell, sizeof(acc[0].shell), "/bin/sh");
    printf("\nSet up the administrator account.\n");
    if (ask_password("root", acc[0].password, sizeof(acc[0].password)) < 0)
        return -1;

    for (tries = 0; tries < 3; tries++) {
        printf("\nUsername for a regular account (leave empty to skip): ");
        if (readline(name, sizeof(name)) < 0)
            return -1;
        if (!name[0])
            return 1;
        if (valid_username(name))
            break;
        printf("Use 1-31 characters from a-z, 0-9, '_' and '-', starting with a letter or '_' (not \"root\").\n");
    }
    if (tries == 3)
        return -1;
    snprintf(acc[1].name, sizeof(acc[1].name), "%s", name);
    snprintf(acc[1].home, sizeof(acc[1].home), "/home/%s", name);
    snprintf(acc[1].shell, sizeof(acc[1].shell), "/bin/sh");
    acc[1].uid = acc[1].gid = 1000;
    if (ask_password(name, acc[1].password, sizeof(acc[1].password)) < 0)
        return -1;
    return 2;
}

static int install_to(const char *name, const struct layout *lay, const struct account *acc, unsigned nacc)
{
    char path[24], guid[37], conf[512];
    struct gpt_part p[2];
    struct fat32 *fs;
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

    printf("Partitioning %s\n", path);
    memset(p, 0, sizeof(p));
    gpt_guid_parse(GPT_TYPE_ESP, p[0].type);
    gpt_random_guid(p[0].guid);
    p[0].first = lay->esp_first;
    p[0].last = lay->esp_first + lay->esp_sectors - 1;
    strcpy(p[0].name, "EFI System");
    gpt_guid_parse(GPT_TYPE_LEMON, p[1].type);
    gpt_random_guid(p[1].guid);
    p[1].first = lay->root_first;
    p[1].last = lay->root_first + lay->root_sectors - 1;
    strcpy(p[1].name, "nirmala-root");
    if (gpt_write(fd, lay->total, p, 2) < 0 || zero_range(fd, p[1].first, 1u << 20) < 0) {
        dprintf(2, "install: cannot write the partition table\n");
        return 1;
    }

    printf("Creating the boot partition\n");
    if (fat32_format(fd, p[0].first, lay->esp_sectors, "NIRMALAESP") < 0) {
        dprintf(2, "install: cannot format the boot partition\n");
        return 1;
    }
    fs = fat32_open(fd, p[0].first, lay->esp_sectors, 1);
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

    printf("Creating the root filesystem and user accounts\n");
    if (mkroot(fd, p[1].first, lay->root_sectors, acc, nacc) < 0) {
        zero_range(fd, p[1].first, 1u << 20);
        return 1;
    }
    sync();
    ioctl(fd, BLKRRPART, 0);
    close(fd);
    printf("\nInstallation finished.\n"
           "Shut down, remove the install media and boot this disk in UEFI mode.\n");
    return 0;
}

static int run(int argc, char **argv)
{
    char names[16][8], answer[64], choice[16];
    struct account acc[MKROOT_MAX_ACCOUNTS];
    struct layout lay;
    const char *target = argc > 1 ? argv[1] : NULL;
    uint64_t total = 0;
    int count, nacc, in_use = 0, i, found = 0, r;

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
        if (readline(answer, sizeof(answer)) < 0)
            return 1;
        if (answer[0] >= '1' && answer[0] <= '9' && atoi(answer) <= count)
            snprintf(choice, sizeof(choice), "%s", names[atoi(answer) - 1]);
        else
            snprintf(choice, sizeof(choice), "%s", answer);
        target = choice;
    }
    for (i = 0; i < count; i++)
        if (!strcmp(names[i], target))
            found = 1;
    if (!found || probe_disk(target, &total, &in_use) < 0) {
        dprintf(2, "install: no such disk: %s\n", target);
        return 1;
    }
    if (in_use) {
        dprintf(2, "install: %s holds the running system\n", target);
        return 1;
    }
    if (plan_layout(total, &lay) < 0) {
        dprintf(2, "install: disk is too small (needs at least %u MiB)\n", ESP_MIB + MIN_ROOT_MIB + 2);
        return 1;
    }

    nacc = collect_accounts(acc);
    if (nacc < 0) {
        dprintf(2, "install: account setup failed\n");
        memset(acc, 0, sizeof(acc));
        return 1;
    }

    printf("\nInstallation summary\n"
           "  Disk:         %s (%lu MiB, GPT, whole disk)\n"
           "  Partition 1:  EFI System, %u MiB, FAT32 (boot loader, kernel)\n"
           "  Partition 2:  nirmala-root, %lu MiB, lemonfs (/)\n"
           "  Accounts:     %s%s%s\n",
           target, (unsigned long)(total / 2048), ESP_MIB, (unsigned long)(lay.root_sectors / 2048),
           acc[0].name, nacc > 1 ? ", " : "", nacc > 1 ? acc[1].name : "");
    printf("\nAll data on %s will be erased. Type yes to continue: ", target);
    if (readline(answer, sizeof(answer)) < 0 || strcmp(answer, "yes")) {
        printf("Cancelled.\n");
        memset(acc, 0, sizeof(acc));
        return 1;
    }
    r = install_to(target, &lay, acc, (unsigned)nacc);
    memset(acc, 0, sizeof(acc));
    return r;
}

int main(int argc, char **argv)
{
    int r = run(argc, argv);

    if (getpid() == 1) {
        static char *const init_argv[] = { "init", NULL };
        char answer[8];

        printf("Press Enter to continue to the live system.\n");
        readline(answer, sizeof(answer));
        execv("/sbin/init", init_argv);
    }
    return r;
}
