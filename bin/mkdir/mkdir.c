#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>

static int make(const char *path, int parents)
{
    char tmp[1024];
    size_t i, n = strlen(path);

    if (!parents) {
        if (mkdir(path, 0755) < 0) {
            dprintf(2, "mkdir: can't create directory '%s': %s\n", path, strerror(errno));
            return 1;
        }
        return 0;
    }
    if (n >= sizeof(tmp)) {
        dprintf(2, "mkdir: can't create directory '%s': %s\n", path, strerror(ENAMETOOLONG));
        return 1;
    }
    memcpy(tmp, path, n + 1);
    for (i = 1; i <= n; i++) {
        if (tmp[i] == '/' || tmp[i] == 0) {
            char c = tmp[i];

            tmp[i] = 0;
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
                dprintf(2, "mkdir: can't create directory '%s': %s\n", tmp, strerror(errno));
                return 1;
            }
            tmp[i] = c;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i = 1, parents = 0, rc = 0;

    if (i < argc && !strcmp(argv[i], "-p")) {
        parents = 1;
        i++;
    }
    if (i >= argc) {
        dprintf(2, "Usage: mkdir [-p] DIRECTORY...\n");
        return 1;
    }
    for (; i < argc; i++)
        rc |= make(argv[i], parents);
    return rc;
}
