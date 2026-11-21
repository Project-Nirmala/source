#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
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
};

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
        char *envp[6], *argv[2];
        char dbcopy[sizeof(db)];

        say("nirmala login: ");
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
        envp[0] = e_path; envp[1] = e_home; envp[2] = e_user; envp[3] = e_shell; envp[4] = e_term; envp[5] = 0;
        argv[0] = "sh";
        argv[1] = 0;
        chdir(a.home);
        execve(a.shell, argv, envp);
        say("login: cannot start shell\n");
        _exit(1);
    }
}
