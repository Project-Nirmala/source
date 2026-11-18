#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>

static int recursive;

static const char *base_of(const char *p)
{
    const char *b = strrchr(p, '/');

    return b ? b + 1 : p;
}

static int copy_file(const char *src, const char *dst, unsigned mode)
{
    static char buf[4096];
    int in = open(src, O_RDONLY), out;
    long n;

    if (in < 0) {
        dprintf(2, "cp: can't open '%s': %s\n", src, strerror(errno));
        return 1;
    }
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode & 0777);
    if (out < 0) {
        dprintf(2, "cp: can't create '%s': %s\n", dst, strerror(errno));
        close(in);
        return 1;
    }
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        long off = 0;

        while (off < n) {
            long w = write(out, buf + off, (size_t)(n - off));

            if (w <= 0) {
                dprintf(2, "cp: write error: %s\n", strerror(errno));
                close(in);
                close(out);
                return 1;
            }
            off += w;
        }
    }
    close(in);
    close(out);
    if (n < 0) {
        dprintf(2, "cp: read error: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

static int copy_path(const char *src, const char *dst, int depth)
{
    struct stat ss, ds;

    if (stat(src, &ss) < 0) {
        dprintf(2, "cp: can't stat '%s': %s\n", src, strerror(errno));
        return 1;
    }
    if (stat(dst, &ds) == 0 && ds.st_ino == ss.st_ino && !S_ISDIR(ds.st_mode)) {
        dprintf(2, "cp: '%s' and '%s' are the same file\n", src, dst);
        return 1;
    }
    if (!S_ISDIR(ss.st_mode))
        return copy_file(src, dst, ss.st_mode);
    if (!recursive) {
        dprintf(2, "cp: omitting directory '%s'\n", src);
        return 1;
    }
    if (depth > 16) {
        dprintf(2, "cp: '%s': directory nesting too deep\n", src);
        return 1;
    }
    if (mkdir(dst, 0755) < 0 && errno != EEXIST) {
        dprintf(2, "cp: can't create directory '%s': %s\n", dst, strerror(errno));
        return 1;
    }
    {
        char buf[1024];
        int fd = open(src, O_RDONLY), rc = 0;
        long n;

        if (fd < 0) {
            dprintf(2, "cp: can't open '%s': %s\n", src, strerror(errno));
            return 1;
        }
        while ((n = getdents64(fd, buf, sizeof(buf))) > 0) {
            long off = 0;

            while (off < n) {
                const struct dirent *d = (const struct dirent *)(buf + off);
                char s2[1024], d2[1024];

                off += d->d_reclen;
                if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
                    continue;
                if (snprintf(s2, sizeof(s2), "%s/%s", src, d->d_name) >= (int)sizeof(s2) ||
                    snprintf(d2, sizeof(d2), "%s/%s", dst, d->d_name) >= (int)sizeof(d2))
                    continue;
                rc |= copy_path(s2, d2, depth + 1);
            }
        }
        close(fd);
        return rc;
    }
}

int main(int argc, char **argv)
{
    int i = 1, n, rc = 0;
    struct stat st;
    int dst_is_dir;

    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++)
        if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "-R"))
            recursive = 1;
        else {
            dprintf(2, "Usage: cp [-r] SOURCE... DEST\n");
            return 1;
        }
    n = argc - i;
    if (n < 2) {
        dprintf(2, "Usage: cp [-r] SOURCE... DEST\n");
        return 1;
    }
    dst_is_dir = stat(argv[argc - 1], &st) == 0 && S_ISDIR(st.st_mode);
    if (n > 2 && !dst_is_dir) {
        dprintf(2, "cp: target '%s' is not a directory\n", argv[argc - 1]);
        return 1;
    }
    for (; i < argc - 1; i++) {
        char target[1024];
        const char *dst = argv[argc - 1];

        if (dst_is_dir) {
            if (snprintf(target, sizeof(target), "%s/%s", dst, base_of(argv[i])) >= (int)sizeof(target))
                continue;
            dst = target;
        }
        rc |= copy_path(argv[i], dst, 0);
    }
    return rc;
}
