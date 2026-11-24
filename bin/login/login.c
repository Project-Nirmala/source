#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "sha256.h"

#define LINEMAX 128

static void say(const char *s)
{
    write(1, s, strlen(s));
}

static int readline(char *buf, size_t size, int echo)
{
    struct termios t, raw;
    long n;

    tcgetattr(0, &t);
    raw = t;
    if (!echo)
        raw.c_lflag &= ~(unsigned)ECHO;
    tcsetattr(0, TCSANOW, &raw);
    n = read(0, buf, size - 1);
    tcsetattr(0, TCSANOW, &t);
    if (!echo)
        say("\n");
    if (n <= 0)
        return -1;
    if (buf[n - 1] == '\n')
        n--;
    buf[n] = 0;
    return (int)n;
}

static void motd(void)
{
    char buf[256];
    long n;
    int fd = open("/etc/motd", O_RDONLY);

    if (fd < 0)
        return;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(1, buf, (size_t)n);
    close(fd);
}

struct account {
    char *salt, *hash, *home, *shell;
    unsigned uid, gid;
};

static unsigned number(const char *s)
{
    unsigned v = 0;

    while (*s >= '0' && *s <= '9')
        v = v * 10 + (unsigned)(*s++ - '0');
    return v;
}

/* First line of a small settings file, or an empty string. */
static void first_line(const char *path, char *out, size_t size)
{
    long n = 0;
    int fd = open(path, O_RDONLY);
    size_t i;

    out[0] = 0;
    if (fd < 0)
        return;
    n = read(fd, out, size - 1);
    close(fd);
    if (n < 0)
        n = 0;
    out[n] = 0;
    for (i = 0; out[i] && out[i] != '\n'; i++)
        ;
    out[i] = 0;
}

static int lookup(const char *user, char *db, struct account *a)
{
    char *line = db;

    while (*line) {
        char *f[7], *p = line;
        int i = 0;

        f[i++] = p;
        while (*p && *p != '\n') {
            if (*p == ':' && i < 7) {
                *p = 0;
                f[i++] = p + 1;
            }
            p++;
        }
        if (*p == '\n')
            *p++ = 0;
        if (i == 7 && !strcmp(f[0], user)) {
            a->salt = f[1];
            a->hash = f[2];
            a->home = f[5];
            a->uid = number(f[3]);
            a->gid = number(f[4]);
            a->shell = f[6];
            return 1;
        }
        line = p;
    }
    return 0;
}

static int check_password(const struct account *a, const char *pw)
{
    struct sha256 s;
    char hex[65];
    int i, diff = 0;

    sha256_init(&s);
    sha256_update(&s, a->salt, strlen(a->salt));
    sha256_update(&s, pw, strlen(pw));
    sha256_hex(&s, hex);
    for (i = 0; i < 64; i++)
        diff |= hex[i] ^ a->hash[i];
    return diff == 0 && a->hash[64] == 0;
}

static void kv(char *dst, size_t size, const char *k, const char *v)
{
    size_t i = 0;

    while (*k && i + 1 < size)
        dst[i++] = *k++;
    while (*v && i + 1 < size)
        dst[i++] = *v++;
    dst[i] = 0;
}

int main(void)
{
    static char db[1024];
    char user[LINEMAX], pw[LINEMAX];
    struct account a;
    int fd;
    long n = 0, r;

    fd = open("/etc/passwd", O_RDONLY);
    if (fd >= 0) {
        while (n < (long)sizeof(db) - 1 && (r = read(fd, db + n, sizeof(db) - 1 - (size_t)n)) > 0)
            n += r;
        close(fd);
    }
    db[n] = 0;

    for (;;) {
        char e_path[] = "PATH=/bin:/sbin", e_term[] = "TERM=linux", e_home[96], e_user[96], e_shell[96];
        char e_lang[64], e_tz[64], loc[48], tz[48], host[65];
        char *envp[8], *argv[2];
        int ne = 0;
        char dbcopy[sizeof(db)];

        if (gethostname(host, sizeof(host)) < 0)
            host[0] = 0;
        say(host[0] ? host : "nirmala");
        say(" login: ");
        if (readline(user, sizeof(user), 1) < 0)
            _exit(1);
        if (!user[0])
            continue;
        say("Password: ");
        if (readline(pw, sizeof(pw), 0) < 0)
            _exit(1);

        memcpy(dbcopy, db, sizeof(db));
        if (!lookup(user, dbcopy, &a) || !check_password(&a, pw)) {
            usleep(1000000);
            say("Login incorrect\n");
            continue;
        }

        motd();
        kv(e_home, sizeof(e_home), "HOME=", a.home);
        kv(e_user, sizeof(e_user), "USER=", user);
        kv(e_shell, sizeof(e_shell), "SHELL=", a.shell);
        first_line("/etc/locale", loc, sizeof(loc));
        first_line("/etc/timezone", tz, sizeof(tz));
        envp[ne++] = e_path; envp[ne++] = e_home; envp[ne++] = e_user; envp[ne++] = e_shell; envp[ne++] = e_term;
        if (loc[0]) {
            kv(e_lang, sizeof(e_lang), "LANG=", loc);
            envp[ne++] = e_lang;
        }
        if (tz[0]) {
            kv(e_tz, sizeof(e_tz), "TZ=", tz);
            envp[ne++] = e_tz;
        }
        envp[ne] = 0;
        argv[0] = strrchr(a.shell, '/') ? strrchr(a.shell, '/') + 1 : a.shell;
        argv[1] = 0;
        /* Drop root before the shell starts: group first, then user, and never continue if either fails. */
        if (a.uid != 0 || a.gid != 0) {
            if (setgid(a.gid) < 0 || setuid(a.uid) < 0 || getuid() != a.uid || getgid() != a.gid) {
                say("login: cannot drop privileges\n");
                _exit(1);
            }
        }
        chdir(a.home);
        setpgid(0, 0);
        tcsetpgrp(0, getpgrp());
        execve(a.shell, argv, envp);
        if (strcmp(a.shell, "/bin/sh")) {
            argv[0] = "sh";
            execve("/bin/sh", argv, envp);
        }
        say("login: cannot start shell\n");
        _exit(1);
    }
}
