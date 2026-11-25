#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/blkio.h>
#include <sys/ioctl.h>

#define PAGE_BYTES  4096u
#define MIN_PAGES   2u
#define OFF_VERSION 1024u
#define OFF_LAST    1028u
#define OFF_MAGIC   4086u

static int in_use(int fd, unsigned long req)
{
    int v = 0;

    ioctl(fd, req, &v);
    return v;
}

int main(int argc, char **argv)
{
    uint8_t hdr[PAGE_BYTES];
    uint64_t bytes = 0, pages;
    uint32_t last;
    unsigned i;
    int fd;

    if (argc != 2) {
        fprintf(stderr, "usage: mkswap device\n");
        return 1;
    }
    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "mkswap: %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    if (ioctl(fd, BLKGETSIZE64, &bytes) < 0 || !bytes) {
        fprintf(stderr, "mkswap: %s: cannot determine the size\n", argv[1]);
        return 1;
    }
    if (in_use(fd, NIR_BLKROOT) || in_use(fd, NIR_BLKSWAP)) {
        fprintf(stderr, "mkswap: %s is in use\n", argv[1]);
        return 1;
    }
    pages = bytes / PAGE_BYTES;
    if (pages < MIN_PAGES) {
        fprintf(stderr, "mkswap: %s is too small\n", argv[1]);
        return 1;
    }
    last = pages - 1 > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)(pages - 1);
    memset(hdr, 0, sizeof(hdr));
    hdr[OFF_VERSION] = 1;
    for (i = 0; i < 4; i++)
        hdr[OFF_LAST + i] = (uint8_t)(last >> (8 * i));
    memcpy(hdr + OFF_MAGIC, "SWAPSPACE2", 10);
    if (write(fd, hdr, sizeof(hdr)) != (long)sizeof(hdr)) {
        fprintf(stderr, "mkswap: %s: write failed: %s\n", argv[1], strerror(errno));
        return 1;
    }
    close(fd);
    printf("Swap space set up on %s: %lu KiB\n", argv[1], (unsigned long)((uint64_t)last * PAGE_BYTES / 1024));
    return 0;
}
