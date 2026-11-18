#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

int main(int argc, char **argv)
{
    struct termios old, raw;
    struct winsize ws;
    char buf[32];
    int len = 0;
    int rows = 0;
    int cols = 0;
    char c;

    if (argc == 3) {
        rows = atoi(argv[1]);
        cols = atoi(argv[2]);
    } else {
        if (ioctl(0, TCGETS, &old) < 0)
            return 1;
        raw = old;
        raw.c_lflag &= ~(ICANON | ECHO);
        ioctl(0, TCSETS, &raw);
        write(1, "\x1b[s\x1b[999;999H\x1b[6n\x1b[u", 21);
        while (len < (int)sizeof(buf) - 1 && read(0, &c, 1) == 1) {
            if (c == 'R')
                break;
            buf[len++] = c;
        }
        buf[len] = '\0';
        ioctl(0, TCSETS, &old);
        for (len = 0; buf[len] && buf[len] != '['; len++)
            ;
        if (!buf[len])
            return 1;
        for (len++; buf[len] >= '0' && buf[len] <= '9'; len++)
            rows = rows * 10 + (buf[len] - '0');
        if (buf[len] != ';')
            return 1;
        for (len++; buf[len] >= '0' && buf[len] <= '9'; len++)
            cols = cols * 10 + (buf[len] - '0');
    }

    if (rows < 1 || cols < 1)
        return 1;
    ws.ws_row = (unsigned short)rows;
    ws.ws_col = (unsigned short)cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    if (ioctl(0, TIOCSWINSZ, &ws) < 0)
        return 1;
    printf("%dx%d\n", cols, rows);
    return 0;
}
