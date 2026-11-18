#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int i, rc = 0;

    if (argc < 2) {
        dprintf(2, "Usage: rmdir DIRECTORY...\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (rmdir(argv[i]) < 0) {
            dprintf(2, "rmdir: '%s': %s\n", argv[i], strerror(errno));
            rc = 1;
        }
    }
    return rc;
}
