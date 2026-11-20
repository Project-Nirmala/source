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

static const char *type_name(const uint8_t *t)
{
    uint8_t g[16];

    if (!gpt_guid_parse(GPT_TYPE_ESP, g) && !memcmp(g, t, 16))
        return "EFI System";
    if (!gpt_guid_parse(GPT_TYPE_LEMON, g) && !memcmp(g, t, 16))
        return "LemonFS";
    if (!gpt_guid_parse(GPT_TYPE_LINUX, g) && !memcmp(g, t, 16))
        return "Linux data";
    return "unknown";
}

static int do_print(int fd, uint64_t total)
{
    struct gpt_part parts[GPT_MAX_PARTS];
    unsigned n, i;

    if (gpt_read(fd, total, parts, GPT_MAX_PARTS, &n) < 0) {
        dprintf(2, "gpt: no valid GUID partition table\n");
        return 1;
    }
    printf("%-3s %10s %10s %9s  %-11s %s\n", "NUM", "FIRST", "LAST", "MiB", "TYPE", "NAME");
    for (i = 0; i < n; i++)
        printf("%-3u %10lu %10lu %9lu  %-11s %s\n", i + 1, (unsigned long)parts[i].first,
               (unsigned long)parts[i].last, (unsigned long)((parts[i].last - parts[i].first + 1) / 2048),
               type_name(parts[i].type), parts[i].name);
    return 0;
}

static int do_create(int fd, uint64_t total, unsigned long esp_mib)
{
    struct gpt_part p[2];
    uint64_t esp_sectors = (uint64_t)esp_mib * 2048, first = 2048;
    int root = 0;

    if (ioctl(fd, NIR_BLKROOT, &root) == 0 && root) {
        dprintf(2, "gpt: refusing to repartition the disk the system is running from\n");
        return 1;
    }
    if (esp_mib < 34 || total < first + esp_sectors + 2048 + 34) {
        dprintf(2, "gpt: disk too small or ESP below 34 MiB\n");
        return 1;
    }
    memset(p, 0, sizeof(p));
    gpt_guid_parse(GPT_TYPE_ESP, p[0].type);
    gpt_random_guid(p[0].guid);
    p[0].first = first;
    p[0].last = first + esp_sectors - 1;
    strcpy(p[0].name, "EFI System");
    gpt_guid_parse(GPT_TYPE_LEMON, p[1].type);
    gpt_random_guid(p[1].guid);
    p[1].first = p[0].last + 1;
    p[1].last = total - 34;
    strcpy(p[1].name, "NirmalaSYS root");
    if (gpt_write(fd, total, p, 2) < 0) {
        dprintf(2, "gpt: write failed\n");
        return 1;
    }
    sync();
    ioctl(fd, BLKRRPART, 0);
    return do_print(fd, total);
}

int main(int argc, char **argv)
{
    int fd, r = 1;
    uint64_t total;

    if (argc < 3) {
        dprintf(2, "usage: gpt DEVICE print\n       gpt DEVICE create [esp-mib]\n");
        return 1;
    }
    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        dprintf(2, "gpt: cannot open %s\n", argv[1]);
        return 1;
    }
    total = device_sectors(fd);
    if (!strcmp(argv[2], "print"))
        r = do_print(fd, total);
    else if (!strcmp(argv[2], "create"))
        r = do_create(fd, total, argc > 3 ? strtoul(argv[3], NULL, 10) : 100);
    else
        dprintf(2, "gpt: unknown command %s\n", argv[2]);
    close(fd);
    return r;
}
