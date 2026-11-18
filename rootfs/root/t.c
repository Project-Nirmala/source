#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

static int failures = 0;

static void check(int cond, const char *msg)
{
    if (cond) {
        printf("[PASS] %s\n", msg);
    } else {
        printf("[FAIL] %s\n", msg);
        failures++;
    }
}

static void test_stdio(void)
{
    char buf[64];
    int n;

    printf("-- stdio --\n");
    check(printf("hello %s, x=%d\n", "nirmala", 42) > 0,
          "printf basic formatting");

    n = snprintf(buf, sizeof(buf), "n=%d", 123);
    check(n == 5 && strcmp(buf, "n=123") == 0, "snprintf");
}

static void test_string(void)
{
    char a[32] = "foo";
    char *dup;

    printf("-- string --\n");
    strcat(a, "bar");
    check(strcmp(a, "foobar") == 0, "strcat/strcmp");
    check(strlen(a) == 6, "strlen");
    check(strcmp(strchr(a, 'b'), "bar") == 0, "strchr");
    check(strstr(a, "oba") != NULL, "strstr");

    dup = strdup(a);
    check(dup != NULL && strcmp(dup, a) == 0, "strdup");
    free(dup);
}

static void test_stdlib(void)
{
    int *arr;
    int *big;
    int ok = 1;
    int i, j, t;

    printf("-- stdlib --\n");
    check(atoi("123") == 123, "atoi");
    check(strtol("ff", NULL, 16) == 255, "strtol base16");

    arr = malloc(5 * sizeof(int));
    check(arr != NULL, "malloc");
    if (arr == NULL)
        return;

    for (i = 0; i < 5; i++)
        arr[i] = 5 - i;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4 - i; j++) {
            if (arr[j] > arr[j + 1]) {
                t = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = t;
            }
        }
    }
    for (i = 0; i < 5; i++) {
        if (arr[i] != i + 1)
            ok = 0;
    }
    check(ok, "malloc'd buffer read/write + sort");

    big = realloc(arr, 10 * sizeof(int));
    check(big != NULL, "realloc");
    if (big)
        free(big);
    else
        free(arr);
}

static void test_file_io(void)
{
    const char *path = "/tmp/t_selftest.txt";
    const char *msg = "nirmala toolchain test\n";
    char rbuf[64];
    ssize_t w, r;
    int fd;

    printf("-- file I/O --\n");
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    check(fd >= 0, "open O_CREAT|O_TRUNC");
    if (fd < 0)
        return;

    w = write(fd, msg, strlen(msg));
    check(w == (ssize_t)strlen(msg), "write");
    close(fd);

    fd = open(path, O_RDONLY, 0);
    check(fd >= 0, "open O_RDONLY");
    if (fd >= 0) {
        memset(rbuf, 0, sizeof(rbuf));
        r = read(fd, rbuf, sizeof(rbuf) - 1);
        check(r == w && strcmp(rbuf, msg) == 0, "read back matches");
        close(fd);
    }
    unlink(path);
}

static void test_process(void)
{
    int fds[2];
    int status = 0;
    char rbuf[32];
    const char *msg = "child-ok";
    ssize_t r;
    pid_t pid, w;

    printf("-- fork/exec/pipe --\n");
    check(pipe(fds) == 0, "pipe");

    pid = fork();
    check(pid >= 0, "fork");

    if (pid == 0) {
        close(fds[0]);
        write(fds[1], msg, strlen(msg));
        close(fds[1]);
        _exit(0);
    } else if (pid > 0) {
        close(fds[1]);
        memset(rbuf, 0, sizeof(rbuf));
        r = read(fds[0], rbuf, sizeof(rbuf) - 1);
        close(fds[0]);

        w = waitpid(pid, &status, 0);

        check(r > 0 && strcmp(rbuf, "child-ok") == 0, "pipe IPC from child");
        check(w == pid, "waitpid returns child pid");
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child exited normally");
    }
}

int main(void)
{
    printf("=== nircc/libc selftest (t.c) ===\n");

    test_stdio();
    test_string();
    test_stdlib();
    test_file_io();
    test_process();

    printf("=== %s (%d failure%s) ===\n",
           failures == 0 ? "ALL PASS" : "SOME FAILED",
           failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
