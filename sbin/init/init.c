#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <netcfg.h>

static pid_t spawn(const char *path, char *const argv[], char *const envp[])
{
    pid_t pid = fork();

    if (pid == 0) {
        execve(path, argv, envp);
        _exit(127);
    }
    return pid;
}

/* First line of a small settings file into 'out'; returns its length, or -1 if the file is missing/empty. */
static int setting(const char *path, char *out, size_t size)
{
    long n;
    int fd = open(path, O_RDONLY), i;

    if (fd < 0)
        return -1;
    n = read(fd, out, size - 1);
    close(fd);
    if (n <= 0)
        return -1;
    out[n] = 0;
    for (i = 0; out[i] && out[i] != '\n'; i++)
        ;
    out[i] = 0;
    return i ? i : -1;
}

/*
 * Settings written by the installer. Every file is optional: a live system
 * or an older install simply keeps the kernel defaults (US keymap, DHCP).
 */
static void apply_settings(void)
{
    char line[96], *f[4];
    int n, i;

    if ((n = setting("/etc/hostname", line, sizeof(line))) > 0)
        sethostname(line, (size_t)n);
    if (setting("/etc/keymap", line, sizeof(line)) > 0)
        setkeymap(line);
    if (setting("/etc/network", line, sizeof(line)) > 0 && !strncmp(line, "static ", 7)) {
        struct netcfg cfg;
        char *p = line + 7;

        /* "static IP MASK GATEWAY DNS": split on single spaces */
        memset(&cfg, 0, sizeof(cfg));
        for (i = 0; i < 4; i++) {
            f[i] = p;
            while (*p && *p != ' ')
                p++;
            if (*p)
                *p++ = 0;
        }
        if (!f[3][0] || ip_parse(f[0], &cfg.ip) || ip_parse(f[1], &cfg.mask) || ip_parse(f[2], &cfg.gw) ||
            ip_parse(f[3], &cfg.dns) || netset(&cfg) < 0)
            printf("init: cannot apply the static network settings\n");
    }
}

int main(int argc, char **argv, char **envp)
{
    static char *const login_argv[] = { "login", 0 };
    pid_t pid, login;
    int st;

    (void)argc;
    (void)argv;

#ifdef CONFIG_SELFTEST
    {
        static char *const test_argv[] = { "selftest", 0 };

        pid = spawn("/bin/selftest", test_argv, envp);
        if (pid > 0 && waitpid(pid, &st, 0) == pid)
            printf("init: selftest exit status %d\n", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    }
#endif

    mkdir("/tmp", 01777);
    mkdir("/root", 0700);
    apply_settings();

    login = spawn("/bin/login", login_argv, envp);
    for (;;) {
        pid = wait(&st);
        if (pid < 0) {
            usleep(1000000);
            continue;
        }
        if (pid == login) {
            usleep(100000);
            login = spawn("/bin/login", login_argv, envp);
        }
    }
}
