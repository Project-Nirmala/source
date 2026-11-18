#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>

static int passed;
static int total;
static int global_value = 1;

static void check(const char *name, int ok)
{
    total++;
    if (ok)
        passed++;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

static int collect(pid_t pid)
{
    int st = -1;

    if (waitpid(pid, &st, 0) != pid)
        return -1;
    return st;
}

static int dir_has(const char *buf, long len, const char *name)
{
    long off = 0;

    while (off < len) {
        const struct dirent *d = (const struct dirent *)(buf + off);

        if (!strcmp(d->d_name, name))
            return 1;
        off += d->d_reclen;
    }
    return 0;
}

static void test_fork_exit(void)
{
    pid_t pid = fork();
    int st;

    if (pid == 0)
        _exit(42);
    st = collect(pid);
    check("fork + wait returns exit status", st >= 0 && WIFEXITED(st) && WEXITSTATUS(st) == 42);
}

static void test_wnohang(void)
{
    pid_t pid = fork();
    int st = 0;

    if (pid == 0) {
        usleep(200000);
        _exit(7);
    }
    check("wait4 WNOHANG on running child returns 0", waitpid(pid, &st, WNOHANG) == 0);
    st = collect(pid);
    check("blocking wait collects slow child", WIFEXITED(st) && WEXITSTATUS(st) == 7);
}

static void test_echild(void)
{
    int st;

    check("wait without children fails with ECHILD", waitpid(-1, &st, 0) < 0 && errno == ECHILD);
}

static void test_signal_status(void)
{
    pid_t pid = fork();
    int st;

    if (pid == 0) {
        int *volatile bad = (int *)0x10;

        *bad = 1;
        _exit(0);
    }
    st = collect(pid);
    check("faulting child reports SIGSEGV", st >= 0 && WIFSIGNALED(st) && WTERMSIG(st) == 11);
}

static void test_pipe_basic(void)
{
    int p[2];
    char buf[16];
    pid_t pid;
    long n;

    pipe(p);
    pid = fork();
    if (pid == 0) {
        close(p[0]);
        write(p[1], "ping", 4);
        _exit(0);
    }
    close(p[1]);
    n = read(p[0], buf, sizeof(buf));
    check("pipe delivers data from child", n == 4 && !memcmp(buf, "ping", 4));
    n = read(p[0], buf, sizeof(buf));
    check("pipe read returns EOF after writers close", n == 0);
    collect(pid);
    close(p[0]);
}

static void test_epipe(void)
{
    int p[2];
    long n;

    pipe(p);
    close(p[0]);
    n = write(p[1], "x", 1);
    check("write to pipe without readers fails with EPIPE", n < 0 && errno == EPIPE);
    close(p[1]);
}

static void test_pipe_large(void)
{
    int p[2];
    char chunk[512];
    char sink[512];
    pid_t pid;
    long total_read = 0, n;
    int i;

    memset(chunk, 'z', sizeof(chunk));
    pipe(p);
    pid = fork();
    if (pid == 0) {
        close(p[0]);
        for (i = 0; i < 24; i++)
            write(p[1], chunk, sizeof(chunk));
        _exit(0);
    }
    close(p[1]);
    while ((n = read(p[0], sink, sizeof(sink))) > 0)
        total_read += n;
    collect(pid);
    close(p[0]);
    check("writer blocks on full pipe and all 12288 bytes arrive", total_read == 24 * 512);
}

static void test_dup2_exec(void)
{
    static char *const args[] = { "utest", 0 };
    static char *const env[] = { 0 };
    int p[2];
    char buf[128];
    pid_t pid;
    long n, got = 0;
    int st;

    pipe(p);
    pid = fork();
    if (pid == 0) {
        dup2(p[1], 1);
        close(p[0]);
        close(p[1]);
        execve("/bin/utest", args, env);
        _exit(127);
    }
    close(p[1]);
    while ((n = read(p[0], buf + got, sizeof(buf) - 1 - (size_t)got)) > 0)
        got += n;
    buf[got] = 0;
    st = collect(pid);
    close(p[0]);
    check("dup2 + exec redirects child stdout into pipe", strstr(buf, "usage") != 0);
    check("exec'd child exit status propagates", WIFEXITED(st) && WEXITSTATUS(st) == 2);
}

static void test_isolation(void)
{
    pid_t pid = fork();

    if (pid == 0) {
        global_value = 2;
        _exit(0);
    }
    collect(pid);
    check("child writes do not affect parent memory", global_value == 1);
}

static void test_shared_offset(void)
{
    char ref[16], a[8], b[8], c[8];
    int fd, rfd, fd2;
    pid_t pid;

    rfd = open("/etc/motd", O_RDONLY);
    read(rfd, ref, sizeof(ref));
    close(rfd);

    fd = open("/etc/motd", O_RDONLY);
    pid = fork();
    if (pid == 0) {
        read(fd, a, 4);
        _exit(0);
    }
    collect(pid);
    read(fd, b, 4);
    check("forked descriptors share the file offset", !memcmp(b, ref + 4, 4));

    fd2 = dup(fd);
    read(fd, b, 3);
    read(fd2, c, 3);
    check("dup shares the file offset", !memcmp(c, ref + 11, 3));
    close(fd);
    close(fd2);
}

static void test_reparent(void)
{
    int p[2];
    char c = 0;
    pid_t pid;

    pipe(p);
    pid = fork();
    if (pid == 0) {
        pid_t g = fork();

        if (g == 0) {
            char v;

            usleep(200000);
            v = (char)('0' + getppid());
            write(p[1], &v, 1);
            _exit(0);
        }
        _exit(0);
    }
    close(p[1]);
    collect(pid);
    read(p[0], &c, 1);
    close(p[0]);
    check("orphaned grandchild is reparented to init", c == '1');
}

static void test_dirs(void)
{
    char buf[1024];
    char cwd[64];
    long n;
    int fd;

    fd = open("/bin", O_RDONLY);
    n = fd >= 0 ? getdents64(fd, buf, sizeof(buf)) : -1;
    check("getdents lists directory entries",
          n > 0 && dir_has(buf, n, ".") && dir_has(buf, n, "..") &&
          dir_has(buf, n, "ttydemo") && dir_has(buf, n, "selftest"));
    if (fd >= 0)
        close(fd);

    check("chdir to a directory succeeds", chdir("/bin") == 0);
    check("getcwd reports the new directory", getcwd(cwd, sizeof(cwd)) && !strcmp(cwd, "/bin"));
    fd = open("utest", O_RDONLY);
    check("relative open resolves against cwd", fd >= 0);
    if (fd >= 0)
        close(fd);
    check("chdir to missing path fails with ENOENT", chdir("/nope") < 0 && errno == ENOENT);
    check("chdir to a file fails with ENOTDIR", chdir("/etc/motd") < 0 && errno == ENOTDIR);
    check("chdir back to root", chdir("/") == 0 && getcwd(cwd, sizeof(cwd)) && !strcmp(cwd, "/"));
}

static int fork_cycle(int i)
{
    int p[2];
    pid_t pid;
    int st;

    if (pipe(p) < 0)
        return 0;
    pid = fork();
    if (pid < 0)
        return 0;
    if (pid == 0) {
        close(p[0]);
        write(p[1], "x", 1);
        _exit(i & 0x7F);
    }
    close(p[0]);
    close(p[1]);
    return waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == (i & 0x7F);
}

static void test_no_leak(void)
{
    struct sysinfo before, after;
    long delta;
    int i, ok = 1;

    for (i = 0; i < 30; i++)
        ok &= fork_cycle(i);
    sysinfo(&before);
    for (i = 0; i < 600; i++)
        ok &= fork_cycle(i);
    sysinfo(&after);
    delta = (long)before.freeram - (long)after.freeram;
    printf("selftest: free memory delta after 600 cycles: %d bytes\n", (int)delta);
    check("600 fork/pipe/exit/wait cycles leak no more than 64 KiB", ok && delta <= 65536);
}

int main(void)
{
    test_fork_exit();
    test_wnohang();
    test_echild();
    test_signal_status();
    test_pipe_basic();
    test_epipe();
    test_pipe_large();
    test_dup2_exec();
    test_isolation();
    test_shared_offset();
    test_reparent();
    test_dirs();
    test_no_leak();
    printf("selftest: %d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}
