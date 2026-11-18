#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

static pid_t spawn(const char *path, char *const argv[], char *const envp[])
{
    pid_t pid = fork();

    if (pid == 0) {
        execve(path, argv, envp);
        _exit(127);
    }
    return pid;
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

    mkdir("/tmp", 0777);
    mkdir("/root", 0700);

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
