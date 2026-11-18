#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <termios.h>
#include <sys/stat.h>
#include <stdlib.h>

#define MAXENT 256
#define NAMEBUF 8192
#define COLS 80

static char names[NAMEBUF];
static char *ent[MAXENT];
static unsigned char isdir[MAXENT];
static int show_all, one_per_line, long_fmt, classify;

static void sort(int n)
{
    int i, j;

    for (i = 1; i < n; i++) {
        char *s = ent[i];
        unsigned char d = isdir[i];

        for (j = i - 1; j >= 0 && strcmp(ent[j], s) > 0; j--) {
            ent[j + 1] = ent[j];
            isdir[j + 1] = isdir[j];
        }
        ent[j + 1] = s;
        isdir[j + 1] = d;
    }
}

static int shown_len(int i)
{
    return (int)strlen(ent[i]) + (classify && isdir[i] ? 1 : 0);
}

static void mode_string(unsigned mode, char *out)
{
    static const char rwx[] = "rwxrwxrwx";
    int i;

    out[0] = S_ISDIR(mode) ? 'd' : S_ISCHR(mode) ? 'c' : '-';
    for (i = 0; i < 9; i++)
        out[1 + i] = (mode & (0400u >> i)) ? rwx[i] : '-';
    out[10] = 0;
}

static void format_time(unsigned long t, char *out)
{
    static const char *const month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    long z = (long)(t / 86400) + 719468, era, doe, yoe, doy, mp, d, m;

    era = z / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    snprintf(out, 16, "%s %2ld %02lu:%02lu", month[m - 1], d, (t % 86400) / 3600, (t % 3600) / 60);
}

static void print_long(const char *dir, int n)
{
    int i;
    unsigned long blocks = 0;
    struct stat sts[MAXENT];

    for (i = 0; i < n; i++) {
        char path[1024];

        if (snprintf(path, sizeof(path), "%s/%s", dir, ent[i]) >= (int)sizeof(path) ||
            stat(path, &sts[i]) < 0)
            memset(&sts[i], 0, sizeof(sts[i]));
        blocks += (unsigned long)(sts[i].st_blocks + 1) / 2;
    }
    printf("total %lu\n", blocks);
    for (i = 0; i < n; i++) {
        char mode[12], when[16];

        mode_string(sts[i].st_mode, mode);
        format_time(sts[i].st_mtime, when);
        printf("%s %4lu %-8s %-8s %9lu %s %s%s\n", mode, (unsigned long)sts[i].st_nlink,
               "root", "root", (unsigned long)sts[i].st_size, when, ent[i],
               classify && isdir[i] ? "/" : "");
    }
}

static void print_entries(int n)
{
    int i, w = 0, cols, rows, r, c;

    if (one_per_line) {
        for (i = 0; i < n; i++)
            printf("%s%s\n", ent[i], classify && isdir[i] ? "/" : "");
        return;
    }
    for (i = 0; i < n; i++)
        if (shown_len(i) > w)
            w = shown_len(i);
    w += 2;
    cols = COLS / w;
    if (cols < 1)
        cols = 1;
    rows = (n + cols - 1) / cols;
    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            int k = c * rows + r;
            char tmp[300];

            if (k >= n)
                break;
            snprintf(tmp, sizeof(tmp), "%s%s", ent[k], classify && isdir[k] ? "/" : "");
            printf("%-*s", k + rows < n ? w : 0, tmp);
        }
        write(1, "\n", 1);
    }
}

static int list(const char *path)
{
    char buf[1024];
    int fd = open(path, O_RDONLY), n = 0;
    size_t used = 0;
    long len;

    if (fd < 0) {
        dprintf(2, "ls: %s: %s\n", path, strerror(errno));
        return 1;
    }
    while ((len = getdents64(fd, buf, sizeof(buf))) > 0) {
        long off = 0;

        while (off < len) {
            const struct dirent *d = (const struct dirent *)(buf + off);
            size_t l = strlen(d->d_name) + 1;

            off += d->d_reclen;
            if (!show_all && d->d_name[0] == '.')
                continue;
            if (n >= MAXENT || used + l > NAMEBUF)
                continue;
            memcpy(names + used, d->d_name, l);
            ent[n] = names + used;
            isdir[n] = d->d_type == DT_DIR;
            used += l;
            n++;
        }
    }
    close(fd);
    if (len < 0) {
        struct stat st;

        if (long_fmt && stat(path, &st) == 0) {
            char mode[12], when[16];

            mode_string(st.st_mode, mode);
            format_time(st.st_mtime, when);
            printf("%s %4lu %-8s %-8s %9lu %s %s\n", mode, (unsigned long)st.st_nlink,
                   "root", "root", (unsigned long)st.st_size, when, path);
            return 0;
        }
        printf("%s\n", path);
        return 0;
    }
    sort(n);
    if (long_fmt)
        print_long(path, n);
    else
        print_entries(n);
    return 0;
}

int main(int argc, char **argv)
{
    int i, paths = 0, rc = 0, first = 1;
    struct termios t;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1]) {
            const char *f;

            for (f = argv[i] + 1; *f; f++) {
                if (*f == 'a')
                    show_all = 1;
                else if (*f == '1')
                    one_per_line = 1;
                else if (*f == 'l')
                    long_fmt = 1;
                else if (*f == 'F')
                    classify = 1;
                else {
                    dprintf(2, "ls: invalid option -- '%c'\n", *f);
                    dprintf(2, "Usage: ls [-1alF] [FILE]...\n");
                    return 1;
                }
            }
        } else {
            paths++;
        }
    }
    if (tcgetattr(1, &t) < 0)
        one_per_line = 1;
    if (!paths)
        return list(".");
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1])
            continue;
        if (paths > 1) {
            printf("%s%s:\n", first ? "" : "\n", argv[i]);
            first = 0;
        }
        rc |= list(argv[i]);
    }
    return rc;
}
