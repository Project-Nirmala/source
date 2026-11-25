#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/blkio.h>
#include <diskfmt.h>
#include <tz.h>
#include <netcfg.h>
#include <sys/reboot.h>
#include "engine.h"

const char *const inst_step_names[INST_STEPS] = {
    "Writing the partition table",
    "Formatting the boot partition",
    "Installing the boot loader",
    "Installing the kernel",
    "Installing the system image",
    "Creating the root filesystem, accounts and settings",
    "Finishing",
};

static int ip_parse_ok(const char *s, unsigned int *v)
{
    return ip_parse(s, v) == 0;
}

const char *const inst_keymaps[] = { "us", "dvorak", "colemak" };
const int inst_nkeymaps = (int)(sizeof(inst_keymaps) / sizeof(inst_keymaps[0]));
const char *const inst_locales[] = { "en_US.UTF-8", "id_ID.UTF-8", "C" };
const int inst_nlocales = (int)(sizeof(inst_locales) / sizeof(inst_locales[0]));

static int in_list(const char *const *list, int n, const char *s)
{
    int i;

    for (i = 0; i < n; i++)
        if (!strcmp(list[i], s))
            return 1;
    return 0;
}

void inst_config_defaults(struct sysconfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->keymap, sizeof(cfg->keymap), "us");
    snprintf(cfg->locale, sizeof(cfg->locale), "en_US.UTF-8");
    snprintf(cfg->timezone, sizeof(cfg->timezone), "UTC");
    snprintf(cfg->hostname, sizeof(cfg->hostname), "nirmala");
}

/* RFC 1123 label: 1-31 of a-z, 0-9 and '-', not starting or ending with '-'. */
int inst_valid_hostname(const char *s)
{
    size_t i, n = strlen(s);

    if (n == 0 || n > 31 || s[0] == '-' || s[n - 1] == '-')
        return 0;
    for (i = 0; i < n; i++)
        if (!(s[i] >= 'a' && s[i] <= 'z') && !(s[i] >= '0' && s[i] <= '9') && s[i] != '-')
            return 0;
    return 1;
}

int inst_valid_ipv4(const char *s)
{
    unsigned int v;

    return ip_parse_ok(s, &v);
}

int inst_config_check(const struct sysconfig *c, char *err, size_t errsz)
{
    unsigned int v;

    if (!in_list(inst_keymaps, inst_nkeymaps, c->keymap))
        snprintf(err, errsz, "Unknown keyboard layout: %s.", c->keymap);
    else if (!in_list(inst_locales, inst_nlocales, c->locale))
        snprintf(err, errsz, "Unknown locale: %s.", c->locale);
    else if (tz_find(c->timezone) < 0)
        snprintf(err, errsz, "Unknown time zone: %s.", c->timezone);
    else if (!inst_valid_hostname(c->hostname))
        snprintf(err, errsz, "Invalid hostname: %s.", c->hostname);
    else if (c->net_static && (!ip_parse_ok(c->ip, &v) || !ip_parse_ok(c->mask, &v) || !ip_parse_ok(c->gw, &v) ||
                               !ip_parse_ok(c->dns, &v)))
        snprintf(err, errsz, "The static network settings are incomplete or not valid IPv4 addresses.");
    else
        return 0;
    return -1;
}

int inst_power(int off)
{
    return reboot(off ? RB_POWER_OFF : RB_AUTOBOOT);
}

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

static int probe_disk(const char *name, struct inst_disk *d)
{
    char path[24];
    uint64_t bytes = 0;
    int fd, root = 0;

    memset(d, 0, sizeof(*d));
    snprintf(d->name, sizeof(d->name), "%s", name);
    snprintf(path, sizeof(path), "/dev/%s", d->name);
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    ioctl(fd, BLKGETSIZE64, &bytes);
    ioctl(fd, NIR_BLKMODEL, d->model);
    ioctl(fd, NIR_BLKROOT, &root);
    close(fd);
    d->mib = bytes >> 20;
    d->busy = root;
    return 1;
}

int inst_list(struct inst_disk *out, int max)
{
    static const char *const prefix[] = { "sd", "vd" };
    char name[INST_NAME_MAX];
    int count = 0;
    unsigned p, l, c, n;

    for (p = 0; p < 2; p++)
        for (l = 0; l < INST_LETTER_DISKS && count < max; l++) {
            snprintf(name, sizeof(name), "%s%c", prefix[p], (char)('a' + l));
            if (probe_disk(name, &out[count]))
                count++;
        }
    for (c = 0; c < INST_NVME_CTRLS; c++)
        for (n = 1; n <= INST_NVME_NS && count < max; n++) {
            snprintf(name, sizeof(name), "nvme%un%u", c, n);
            if (probe_disk(name, &out[count]))
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
    return inst_plan_ex(name, 0, 0, 0, lay, err, errsz);
}

int inst_plan_ex(const char *name, unsigned esp_mib, unsigned swap_mib, unsigned root_mib, struct inst_layout *lay,
                 char *err, size_t errsz)
{
    struct inst_disk disks[INST_MAX_DISKS];
    char path[24];
    uint64_t total, avail;
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
    if (!esp_mib)
        esp_mib = INST_ESP_MIB;
    if (esp_mib < INST_MIN_ESP_MIB || esp_mib > INST_MAX_ESP_MIB) {
        snprintf(err, errsz, "The boot partition must be %u-%u MiB.", INST_MIN_ESP_MIB, INST_MAX_ESP_MIB);
        return -1;
    }
    if (swap_mib > 1024u * 1024u || root_mib > 1024u * 1024u * 1024u) {
        snprintf(err, errsz, "A partition size is out of range.");
        return -1;
    }
    memset(lay, 0, sizeof(*lay));
    lay->total = total;
    lay->esp_first = 2048;
    lay->esp_sectors = (uint64_t)esp_mib * 2048;
    if (swap_mib) {
        lay->swap_first = lay->esp_first + lay->esp_sectors;
        lay->swap_sectors = (uint64_t)swap_mib * 2048;
    }
    lay->root_first = lay->esp_first + lay->esp_sectors + lay->swap_sectors;
    if (total < lay->root_first + (uint64_t)INST_MIN_ROOT_MIB * 2048 + 34) {
        snprintf(err, errsz, "The disk is too small. At least %u MiB is needed.",
                 esp_mib + swap_mib + INST_MIN_ROOT_MIB + 2);
        return -1;
    }
    avail = (total - 34) - lay->root_first + 1;
    if (root_mib) {
        if (root_mib < INST_MIN_ROOT_MIB || (uint64_t)root_mib * 2048 > avail) {
            snprintf(err, errsz, "The root size must be %u-%lu MiB with these other partitions.", INST_MIN_ROOT_MIB,
                     (unsigned long)(avail / 2048));
            return -1;
        }
        lay->root_sectors = (uint64_t)root_mib * 2048;
    } else {
        lay->root_sectors = avail;
    }
    return 0;
}

static int swap_format(int fd, uint64_t first, uint64_t nsect)
{
    uint8_t hdr[4096];
    uint64_t pages = nsect / 8;
    uint32_t last = pages - 1 > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)(pages - 1);
    unsigned i;

    memset(hdr, 0, sizeof(hdr));
    hdr[1024] = 1;
    for (i = 0; i < 4; i++)
        hdr[1028 + i] = (uint8_t)(last >> (8 * i));
    memcpy(hdr + 4086, "SWAPSPACE2", 10);
    if (lseek(fd, (off_t)(first * 512), SEEK_SET) < 0)
        return -1;
    return write(fd, hdr, sizeof(hdr)) == (long)sizeof(hdr) ? 0 : -1;
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
             const struct sysconfig *cfg, inst_progress cb, void *ctx, char *err, size_t errsz)
{
    char path[24], guid[37], conf[512];
    struct gpt_part p[3], *rp;
    struct sysconfig local;
    const struct sysconfig *scfg = cfg;
    struct fat32 *fs;
    unsigned np = 1;
    int fd, root = 0;

    if (cfg && inst_config_check(cfg, err, errsz) < 0)
        return -1;
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
    if (lay->swap_sectors) {
        gpt_guid_parse(GPT_TYPE_SWAP, p[np].type);
        gpt_random_guid(p[np].guid);
        p[np].first = lay->swap_first;
        p[np].last = lay->swap_first + lay->swap_sectors - 1;
        strcpy(p[np].name, "nirmala-swap");
        if (cfg)
            local = *cfg;
        else
            memset(&local, 0, sizeof(local));
        gpt_guid_format(p[np].guid, local.swap_partuuid);
        scfg = &local;
        np++;
    }
    rp = &p[np];
    gpt_guid_parse(GPT_TYPE_LEMON, rp->type);
    gpt_random_guid(rp->guid);
    rp->first = lay->root_first;
    rp->last = lay->root_first + lay->root_sectors - 1;
    strcpy(rp->name, "nirmala-root");
    np++;
    if (gpt_write(fd, lay->total, p, np) < 0 || zero_range(fd, rp->first, 1u << 20) < 0 ||
        (lay->swap_sectors && swap_format(fd, lay->swap_first, lay->swap_sectors) < 0)) {
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
    gpt_guid_format(rp->guid, guid);
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
    if (mkroot(fd, rp->first, lay->root_sectors, acc, nacc, scfg) < 0) {
        zero_range(fd, rp->first, 1u << 20);
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
