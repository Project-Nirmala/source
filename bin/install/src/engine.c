#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/blkio.h>
#include <diskfmt.h>
#include "engine.h"

const char *const inst_step_names[INST_STEPS] = {
    "Writing the partition table",
    "Formatting the boot partition",
    "Installing the boot loader",
    "Installing the kernel",
    "Installing the system image",
    "Creating the root filesystem and accounts",
    "Finishing",
};

static int readable(const char *path)
{
    int fd = open(path, O_RDONLY);

    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

static uint64_t device_sectors(int fd)
{
    uint64_t bytes = 0;

    if (ioctl(fd, BLKGETSIZE64, &bytes) < 0 || bytes == 0)
        bytes = (uint64_t)lseek(fd, 0, SEEK_END);
    return bytes / 512;
}

int inst_check_system(void)
{
    return readable("/dev/kernel") && readable("/dev/initrd") && readable("/usr/share/limine/BOOTX64.EFI");
}

int inst_list(struct inst_disk *out, int max)
{
    static const char *const prefix[] = { "sd", "vd" };
    int count = 0;
    unsigned p, l;

    for (p = 0; p < 2; p++)
        for (l = 0; l < 8 && count < max; l++) {
            char path[24];
            struct inst_disk *d = &out[count];
            uint64_t bytes = 0;
            int fd, root = 0;

            memset(d, 0, sizeof(*d));
            snprintf(d->name, sizeof(d->name), "%s%c", prefix[p], (char)('a' + l));
            snprintf(path, sizeof(path), "/dev/%s", d->name);
            fd = open(path, O_RDONLY);
            if (fd < 0)
                continue;
            ioctl(fd, BLKGETSIZE64, &bytes);
            ioctl(fd, NIR_BLKMODEL, d->model);
            ioctl(fd, NIR_BLKROOT, &root);
            close(fd);
            d->mib = bytes >> 20;
            d->busy = root;
            count++;
        }
    return count;
}

int inst_valid_username(const char *s)
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

int inst_plan(const char *name, struct inst_layout *lay, char *err, size_t errsz)
{
    struct inst_disk disks[INST_MAX_DISKS];
    char path[24];
    uint64_t total;
    int count, i, fd, root = 0, found = 0;

    count = inst_list(disks, INST_MAX_DISKS);
    for (i = 0; i < count; i++)
        if (!strcmp(disks[i].name, name))
            found = 1;
    snprintf(path, sizeof(path), "/dev/%s", name);
    fd = found ? open(path, O_RDONLY) : -1;
    if (fd < 0) {
        snprintf(err, errsz, "No such disk: %s.", name);
        return -1;
    }
    total = device_sectors(fd);
    ioctl(fd, NIR_BLKROOT, &root);
    close(fd);
    if (root) {
        snprintf(err, errsz, "%s holds the running system.", path);
        return -1;
    }
    lay->total = total;
    lay->esp_first = 2048;
    lay->esp_sectors = (uint64_t)INST_ESP_MIB * 2048;
    lay->root_first = lay->esp_first + lay->esp_sectors;
    if (total < lay->root_first + (uint64_t)INST_MIN_ROOT_MIB * 2048 + 34) {
        snprintf(err, errsz, "The disk is too small. At least %u MiB is needed.", INST_ESP_MIB + INST_MIN_ROOT_MIB + 2);
        return -1;
    }
    lay->root_sectors = (total - 34) - lay->root_first + 1;
    return 0;
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

static int put_file(struct fat32 *fs, const char *dest, const char *src_path)
{
    int src = open(src_path, O_RDONLY), r;

    if (src < 0)
        return -1;
    r = fat32_put_fd(fs, dest, src);
    close(src);
    return r;
}

int inst_run(const char *name, const struct inst_layout *lay, const struct account *acc, unsigned nacc,
             inst_progress cb, void *ctx, char *err, size_t errsz)
{
    char path[24], guid[37], conf[512];
    struct gpt_part p[2];
    struct fat32 *fs;
    int fd, root = 0;

    snprintf(path, sizeof(path), "/dev/%s", name);
    fd = open(path, O_RDWR);
    if (fd < 0) {
        snprintf(err, errsz, "Cannot open %s.", path);
        return -1;
    }
    if (ioctl(fd, NIR_BLKROOT, &root) == 0 && root) {
        snprintf(err, errsz, "%s holds the running system.", path);
        close(fd);
        return -1;
    }

    cb(0, ctx);
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
        snprintf(err, errsz, "Cannot write the partition table.");
        goto fail;
    }

    cb(1, ctx);
    if (fat32_format(fd, p[0].first, lay->esp_sectors, "NIRMALAESP") < 0) {
        snprintf(err, errsz, "Cannot format the boot partition.");
        goto fail;
    }
    fs = fat32_open(fd, p[0].first, lay->esp_sectors, 1);
    if (!fs) {
        snprintf(err, errsz, "Cannot open the boot partition.");
        goto fail;
    }
    gpt_guid_format(p[1].guid, guid);
    snprintf(conf, sizeof(conf),
             "timeout: 3\nserial: yes\n\n/NirmalaSYS\n    protocol: limine\n    path: boot():/boot/nirmala\n"
             "    module_path: boot():/boot/initrd.tar\n    cmdline: root=PARTUUID=%s\n", guid);

    cb(2, ctx);
    if (put_file(fs, "/EFI/BOOT/BOOTX64.EFI", "/usr/share/limine/BOOTX64.EFI") < 0 ||
        fat32_put_mem(fs, "/limine.conf", conf, strlen(conf)) < 0) {
        snprintf(err, errsz, "Cannot install the boot loader.");
        fat32_close(fs);
        goto fail;
    }
    cb(3, ctx);
    if (put_file(fs, "/boot/nirmala", "/dev/kernel") < 0) {
        snprintf(err, errsz, "Cannot copy the kernel.");
        fat32_close(fs);
        goto fail;
    }
    cb(4, ctx);
    if (put_file(fs, "/boot/initrd.tar", "/dev/initrd") < 0) {
        snprintf(err, errsz, "Cannot copy the system image.");
        fat32_close(fs);
        goto fail;
    }
    if (fat32_close(fs) < 0) {
        snprintf(err, errsz, "Cannot finish writing the boot partition.");
        goto fail;
    }

    cb(5, ctx);
    if (mkroot(fd, p[1].first, lay->root_sectors, acc, nacc) < 0) {
        zero_range(fd, p[1].first, 1u << 20);
        snprintf(err, errsz, "Cannot create the root filesystem.");
        goto fail;
    }

    cb(6, ctx);
    sync();
    ioctl(fd, BLKRRPART, 0);
    close(fd);
    cb(INST_STEPS, ctx);
    return 0;
fail:
    close(fd);
    return -1;
}
