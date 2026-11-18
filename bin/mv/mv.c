#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>

static const char *base_of(const char *p)
{
    const char *b = strrchr(p, '/');

    return b ? b + 1 : p;
}

int main(int argc, char **argv)
{
    struct stat st;
    int i, rc = 0, dst_is_dir;

    if (argc < 3) {
        dprintf(2, "Usage: mv SOURCE... DEST\n");
        return 1;
    }
    dst_is_dir = stat(argv[argc - 1], &st) == 0 && S_ISDIR(st.st_mode);
    if (argc > 3 && !dst_is_dir) {
        dprintf(2, "mv: target '%s' is not a directory\n", argv[argc - 1]);
        return 1;
    }
    for (i = 1; i < argc - 1; i++) {
        char target[1024];
        const char *dst = argv[argc - 1];

        if (dst_is_dir) {
            if (snprintf(target, sizeof(target), "%s/%s", dst, base_of(argv[i])) >= (int)sizeof(target))
                continue;
            dst = target;
        }
        if (rename(argv[i], dst) < 0) {
            dprintf(2, "mv: can't rename '%s': %s\n", argv[i], strerror(errno));
            rc = 1;
        }
    }
    return rc;
}
