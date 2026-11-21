#include <curses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int vw_printw(WINDOW *win, const char *fmt, va_list ap)
{
    char small[512], *buf = small;
    va_list copy;
    int n, ret;

    if (win == NULL)
        return ERR;
    va_copy(copy, ap);
    n = vsnprintf(small, sizeof(small), fmt, copy);
    va_end(copy);
    if (n < 0)
        return ERR;
    if ((size_t)n >= sizeof(small)) {
        buf = malloc((size_t)n + 1);
        if (!buf)
            return ERR;
        vsnprintf(buf, (size_t)n + 1, fmt, ap);
    }
    ret = n == 0 ? OK : waddnstr(win, buf, n);
    if (buf != small)
        free(buf);
    return ret;
}

int vwprintw(WINDOW *win, const char *fmt, va_list ap)
{
    return vw_printw(win, fmt, ap);
}

int printw(const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vw_printw(stdscr, fmt, ap);
    va_end(ap);
    return ret;
}

int wprintw(WINDOW *win, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vw_printw(win, fmt, ap);
    va_end(ap);
    return ret;
}

int mvprintw(int y, int x, const char *fmt, ...)
{
    va_list ap;
    int ret;

    if (move(y, x) != OK)
        return ERR;
    va_start(ap, fmt);
    ret = vw_printw(stdscr, fmt, ap);
    va_end(ap);
    return ret;
}

int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...)
{
    va_list ap;
    int ret;

    if (wmove(win, y, x) != OK)
        return ERR;
    va_start(ap, fmt);
    ret = vw_printw(win, fmt, ap);
    va_end(ap);
    return ret;
}
