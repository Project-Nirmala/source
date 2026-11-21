#include <stdio.h>
#include <sys/stat.h>

int main(int argc, char **argv)
{
    unsigned mode = 0;
    const char *p;
    int i, rc = 0;

    if (argc < 3) {
        dprintf(2, "usage: chmod OCTAL-MODE file...\n");
        return 2;
    }
    for (p = argv[1]; *p; p++) {
        if (*p < '0' || *p > '7') {
            dprintf(2, "chmod: invalid mode '%s' (octal only)\n", argv[1]);
            return 2;
        }
        mode = mode * 8 + (unsigned)(*p - '0');
    }
    for (i = 2; i < argc; i++)
        if (chmod(argv[i], mode) < 0) {
            dprintf(2, "chmod: cannot change '%s': Operation not permitted or no such file\n", argv[i]);
            rc = 1;
        }
    return rc;
}
