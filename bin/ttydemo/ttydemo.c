#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>

static void become_foreground(void)
{
    setpgid(0, 0);
    tcsetpgrp(0, getpgrp());
}

static void raw_mode_demo(void)
{
    struct termios saved, raw;
    unsigned char c;
    int i;

    tcgetattr(0, &saved);
    raw = saved;
    raw.c_lflag &= ~(unsigned)(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    tcsetattr(0, TCSANOW, &raw);
    printf("raw mode: press 3 keys\n");
    for (i = 0; i < 3; i++) {
        if (read(0, &c, 1) != 1)
            break;
        printf("key 0x%x\n", (unsigned)c);
    }
    tcsetattr(0, TCSANOW, &saved);
}

int main(int argc, char **argv)
{
    char line[128];
    long n;

    (void)argc;
    (void)argv;
    printf("ttydemo: commands are echo text, raw, spin, sleep, quit\n");
    for (;;) {
        write(1, "tty> ", 5);
        n = read(0, line, sizeof(line) - 1);
        if (n < 0) {
            printf("read failed, errno=%d\n", errno);
            continue;
        }
        if (n == 0) {
            printf("EOF\n");
            break;
        }
        if (line[n - 1] == '\n')
            n--;
        line[n] = 0;
        if (n == 0)
            continue;
        if (!strcmp(line, "quit"))
            break;
        if (!strcmp(line, "raw")) {
            raw_mode_demo();
        } else if (!strcmp(line, "spin")) {
            become_foreground();
            printf("spinning, press Ctrl-C\n");
            for (;;)
                ;
        } else if (!strcmp(line, "sleep")) {
            become_foreground();
            printf("sleeping 10s, press Ctrl-C\n");
            if (usleep(10000000) < 0)
                printf("sleep interrupted, errno=%d\n", errno);
        } else {
            printf("you typed: %s (%d bytes)\n", line, (int)n);
        }
    }
    printf("ttydemo: bye\n");
    return 0;
}
