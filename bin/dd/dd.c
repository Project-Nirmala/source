#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_BS 65536

static char buf[MAX_BS];

static unsigned long num(const char *s)
{
    char *end;
    unsigned long v = strtoul(s, &end, 10);

    if (*end == 'k' || *end == 'K')
        v *= 1024;
    else if (*end == 'm' || *end == 'M')
        v *= 1024 * 1024;
    return v;
}

int main(int argc, char **argv)
{
    const char *in = NULL, *out = NULL;
    unsigned long bs = 512, count = 0, skip = 0, seek = 0, blocks = 0, bytes = 0;
    int i, ifd = 0, ofd = 1, notrunc = 0;

    for (i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "if=", 3))
            in = argv[i] + 3;
        else if (!strncmp(argv[i], "of=", 3))
            out = argv[i] + 3;
        else if (!strncmp(argv[i], "bs=", 3))
            bs = num(argv[i] + 3);
        else if (!strncmp(argv[i], "count=", 6))
            count = num(argv[i] + 6);
        else if (!strncmp(argv[i], "skip=", 5))
            skip = num(argv[i] + 5);
        else if (!strncmp(argv[i], "seek=", 5))
            seek = num(argv[i] + 5);
        else if (!strcmp(argv[i], "conv=notrunc"))
            notrunc = 1;
        else {
            dprintf(2, "dd: unknown operand %s\n", argv[i]);
            return 1;
        }
    }
    if (bs == 0 || bs > MAX_BS) {
        dprintf(2, "dd: bs must be between 1 and %d\n", MAX_BS);
        return 1;
    }
    if (in) {
        ifd = open(in, O_RDONLY);
        if (ifd < 0) {
            dprintf(2, "dd: cannot open %s\n", in);
            return 1;
        }
    }
    if (out) {
        ofd = open(out, O_WRONLY | O_CREAT, 0644);
        if (ofd < 0) {
            dprintf(2, "dd: cannot open %s\n", out);
            return 1;
        }
        if (!notrunc && seek == 0)
            ftruncate(ofd, 0);
    }
    if (skip && lseek(ifd, (off_t)(skip * bs), SEEK_SET) < 0) {
        dprintf(2, "dd: cannot seek input\n");
        return 1;
    }
    if (seek && lseek(ofd, (off_t)(seek * bs), SEEK_SET) < 0) {
        dprintf(2, "dd: cannot seek output\n");
        return 1;
    }
    while (!count || blocks < count) {
        unsigned long got = 0;

        while (got < bs) {
            long r = read(ifd, buf + got, bs - got);

            if (r < 0) {
                dprintf(2, "dd: read error\n");
                return 1;
            }
            if (r == 0)
                break;
            got += (unsigned long)r;
        }
        if (got == 0)
            break;
        if (write(ofd, buf, got) != (long)got) {
            dprintf(2, "dd: write error\n");
            return 1;
        }
        bytes += got;
        blocks++;
        if (got < bs)
            break;
    }
    sync();
    dprintf(2, "%lu blocks, %lu bytes copied\n", blocks, bytes);
    return 0;
}
