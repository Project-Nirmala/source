#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>

static int recursive, force;

static int rm_path(const char *path, int depth);

static int empty_dir(const char *path, int depth)
{
    for (;;) {
        char buf[1024];
        int fd = open(path, O_RDONLY), seen = 0, removed = 0;
        long n, off = 0;

        if (fd < 0) {
            dprintf(2, "rm: can't remove '%s': %s\n", path, strerror(errno));
            return 1;
        }
        n = getdents64(fd, buf, sizeof(buf));
        close(fd);
        if (n <= 0)
            return 0;
        while (off < n) {
            const struct dirent *d = (const struct dirent *)(buf + off);
            char child[1024];

            off += d->d_reclen;
            if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
                continue;
            seen++;
            if (snprintf(child, sizeof(child), "%s/%s", path, d->d_name) >= (int)sizeof(child))
                continue;
            if (rm_path(child, depth + 1) == 0)
                removed++;
        }
        if (!seen)
            return 0;
        if (!removed)
            return 1;
    }
}

static int rm_path(const char *path, int depth)
{
    struct stat st;

    if (stat(path, &st) < 0) {
        if (force && errno == ENOENT)
            return 0;
        dprintf(2, "rm: can't remove '%s': %s\n", path, strerror(errno));
        return 1;
    }
    if (S_ISDIR(st.st_mode)) {
        if (!recursive) {
            dprintf(2, "rm: can't remove '%s': %s\n", path, strerror(EISDIR));
            return 1;
        }
        if (depth > 32) {
            dprintf(2, "rm: can't remove '%s': %s\n", path, strerror(ENAMETOOLONG));
            return 1;
        }
        if (empty_dir(path, depth))
            return 1;
        if (rmdir(path) < 0) {
            dprintf(2, "rm: can't remove '%s': %s\n", path, strerror(errno));
            return 1;
        }
        return 0;
    }
    if (unlink(path) < 0) {
        dprintf(2, "rm: can't remove '%s': %s\n", path, strerror(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i = 1, rc = 0, files = 0;

    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *f;

        for (f = argv[i] + 1; *f; f++) {
            if (*f == 'r' || *f == 'R')
                recursive = 1;
            else if (*f == 'f')
                force = 1;
            else {
                dprintf(2, "Usage: rm [-rf] FILE...\n");
                return 1;
            }
        }
    }
    for (; i < argc; i++) {
        files++;
        if (!strcmp(argv[i], "/")) {
            dprintf(2, "rm: it is dangerous to operate recursively on '/'\n");
            rc = 1;
            continue;
        }
        if (!strcmp(argv[i], ".") || !strcmp(argv[i], "..")) {
            dprintf(2, "rm: refusing to remove '.' or '..' directory: skipping '%s'\n", argv[i]);
            rc = 1;
            continue;
        }
        rc |= rm_path(argv[i], 0);
    }
    if (!files && !force) {
        dprintf(2, "Usage: rm [-rf] FILE...\n");
        return 1;
    }
    return rc;
}
