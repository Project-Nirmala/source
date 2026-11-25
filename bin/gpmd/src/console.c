#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/fbtext.h>
#include <sys/termios.h>
#include "gpmd.h"

static int fb_fd = -1;
static int tty_fd = -1;

int console_open(const char *fbpath, const char *ttypath)
{
    fb_fd = open(fbpath, O_RDONLY);
    if (fb_fd < 0)
        return -1;
    tty_fd = open(ttypath, O_RDWR);
    return tty_fd < 0 ? -1 : 0;
}

int console_info(unsigned *cols, unsigned *rows, unsigned *width, unsigned *height)
{
    struct nir_fbinfo info;

    if (ioctl(fb_fd, NIR_FBINFO, &info) < 0 || !info.cols || !info.rows || !info.width || !info.height)
        return -1;
    *cols = info.cols;
    *rows = info.rows;
    *width = info.width;
    *height = info.height;
    return 0;
}

void console_show_pointer(int col, int row)
{
    struct nir_fbptr p;

    p.col = col;
    p.row = row;
    p.visible = 1;
    ioctl(fb_fd, NIR_FBPOINTER, &p);
}

void console_hide_pointer(void)
{
    struct nir_fbptr p;

    p.col = 0;
    p.row = 0;
    p.visible = 0;
    ioctl(fb_fd, NIR_FBPOINTER, &p);
}

void console_highlight(int start, int end)
{
    struct nir_fbsel s;

    s.start = start;
    s.end = end;
    ioctl(fb_fd, NIR_FBSELECT, &s);
}

int console_read_screen(char *buf, int len)
{
    struct nir_fbread rd;

    rd.start = 0;
    rd.len = len;
    rd.buf = buf;
    return ioctl(fb_fd, NIR_FBREAD, &rd) == len ? 0 : -1;
}

int console_paste(const char *text, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        char c = text[i];

        if (ioctl(tty_fd, TIOCSTI, &c) < 0)
            return -1;
    }
    return 0;
}
