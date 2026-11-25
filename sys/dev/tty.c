#include <tty.h>
#include <proc.h>
#include <serial.h>
#include <kern.h>
#include <fbcon.h>
#include <uaccess.h>
#include <sys/termios.h>
#include <sys/errno.h>

#define LINE_MAX_LEN 255
#define QSIZE        1024
#define SIGINT       2

static struct {
    struct termios t;
    uint8_t        line[LINE_MAX_LEN + 1];
    size_t         line_len;
    uint8_t        q[QSIZE];
    size_t         q_head;
    size_t         q_count;
    bool           eof_pending;
    int            fg_pgrp;
    unsigned short rows;
    unsigned short cols;
} tty;

static unsigned short ser_rows, ser_cols;

void tty_set_winsize(unsigned short rows, unsigned short cols)
{
    ser_rows = rows;
    ser_cols = cols;
}

static void echo_char(uint8_t c)
{
    if (tty.t.c_lflag & ECHO)
        serial_putc((char)c);
}

static void echo_erase(void)
{
    if (tty.t.c_lflag & ECHO) {
        serial_putc('\b');
        serial_putc(' ');
        serial_putc('\b');
    }
}

static void q_push(uint8_t c)
{
    if (tty.q_count < QSIZE) {
        tty.q[(tty.q_head + tty.q_count) % QSIZE] = c;
        tty.q_count++;
    }
}

static uint8_t q_pop(void)
{
    uint8_t c = tty.q[tty.q_head];

    tty.q_head = (tty.q_head + 1) % QSIZE;
    tty.q_count--;
    return c;
}

static void commit_line(void)
{
    size_t i;

    for (i = 0; i < tty.line_len; i++)
        q_push(tty.line[i]);
    tty.line_len = 0;
    proc_wakeup(&tty);
}

void tty_init(void)
{
    memset(&tty, 0, sizeof(tty));
    tty.t.c_iflag = ICRNL;
    tty.t.c_oflag = OPOST | ONLCR;
    tty.t.c_cflag = B115200 | CS8 | CREAD;
    tty.t.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK;
    tty.t.c_cc[VINTR]  = 0x03;
    tty.t.c_cc[VQUIT]  = 0x1C;
    tty.t.c_cc[VERASE] = 0x7F;
    tty.t.c_cc[VKILL]  = 0x15;
    tty.t.c_cc[VEOF]   = 0x04;
    tty.t.c_cc[VTIME]  = 0;
    tty.t.c_cc[VMIN]   = 1;
}

static int esc_state;

static bool esc_filtered(uint8_t c)
{
    if (esc_state == 0) {
        if (c != 0x1B)
            return false;
        esc_state = 1;
        return true;
    }
    if (esc_state == 1) {
        esc_state = 0;
        if (c == '[' || c == 'O') {
            esc_state = 2;
            return true;
        }
        return false;
    }
    if (c >= 0x40 && c <= 0x7E)
        esc_state = 0;
    return true;
}

void tty_input(uint8_t c)
{
    uint32_t lf = tty.t.c_lflag;

    if (c == '\r' && (tty.t.c_iflag & ICRNL))
        c = '\n';

    if ((lf & ISIG) && c == tty.t.c_cc[VINTR]) {
        if (lf & ECHO) {
            serial_putc('^');
            serial_putc('C');
            serial_putc('\n');
        }
        tty.line_len = 0;
        if (tty.fg_pgrp > 0)
            proc_signal_pgrp(tty.fg_pgrp, SIGINT);
        return;
    }

    if (!(lf & ICANON)) {
        esc_state = 0;
        q_push(c);
        echo_char(c);
        proc_wakeup(&tty);
        return;
    }

    if (esc_filtered(c))
        return;

    if (c == tty.t.c_cc[VERASE] || c == 0x08) {
        if (tty.line_len > 0) {
            tty.line_len--;
            echo_erase();
        }
    } else if (c == tty.t.c_cc[VKILL]) {
        while (tty.line_len > 0) {
            tty.line_len--;
            echo_erase();
        }
    } else if (c == tty.t.c_cc[VEOF]) {
        if (tty.line_len == 0) {
            tty.eof_pending = true;
            proc_wakeup(&tty);
        } else {
            commit_line();
        }
    } else if (c == '\n') {
        echo_char('\n');
        tty.line[tty.line_len++] = '\n';
        commit_line();
    } else if (c == '\t' || (c >= 0x20 && c < 0x7F)) {
        if (tty.line_len < LINE_MAX_LEN - 1) {
            tty.line[tty.line_len++] = c;
            echo_char(c);
        }
    }
}

long tty_read(void *buf, size_t n)
{
    uint8_t *out = buf;
    size_t got = 0;

    if (n == 0)
        return 0;

    for (;;) {
        if (curproc->pending_sig)
            return -EINTR;
        if (tty.q_count > 0)
            break;
        if (tty.eof_pending) {
            tty.eof_pending = false;
            return 0;
        }
        if (!(tty.t.c_lflag & ICANON) && tty.t.c_cc[VMIN] == 0)
            return 0;
        proc_wait_on(&tty);
    }

    while (got < n && tty.q_count > 0) {
        uint8_t c = q_pop();

        out[got++] = c;
        if ((tty.t.c_lflag & ICANON) && c == '\n')
            break;
    }
    return (long)got;
}

long tty_write(const void *buf, size_t n)
{
    const char *p = buf;
    size_t i;

    for (i = 0; i < n; i++)
        serial_putc(p[i]);
    return (long)n;
}

long tty_ioctl(uint64_t req, uint64_t arg)
{
    struct termios nt;
    struct winsize ws;
    int pid;
    int avail;
    uint8_t byte;

    switch (req) {
    case TIOCSTI:
        if (curproc->uid != 0)
            return -EPERM;
        if (copy_from_user(&byte, arg, sizeof(byte)) < 0)
            return -EFAULT;
        tty_input(byte);
        return 0;
    case FIONREAD:
        avail = (int)tty.q_count;
        return copy_to_user(arg, &avail, sizeof(avail));
    case TCGETS:
        return copy_to_user(arg, &tty.t, sizeof(tty.t));
    case TCSETS:
    case TCSETSW:
    case TCSETSF:
        if (copy_from_user(&nt, arg, sizeof(nt)) < 0)
            return -EFAULT;
        if ((tty.t.c_lflag & ICANON) && !(nt.c_lflag & ICANON))
            commit_line();
        if (req == TCSETSF) {
            tty.q_count = 0;
            tty.line_len = 0;
        }
        tty.t = nt;
        return 0;
    case TIOCGPGRP:
        pid = tty.fg_pgrp;
        return copy_to_user(arg, &pid, sizeof(pid));
    case TIOCSPGRP:
        if (copy_from_user(&pid, arg, sizeof(pid)) < 0)
            return -EFAULT;
        if (pid < 0)
            return -EINVAL;
        tty.fg_pgrp = pid;
        return 0;
    case TIOCGWINSZ:
        ws.ws_row = 24;
        ws.ws_col = 80;
        if (ser_rows && ser_cols) {
            ws.ws_row = ser_rows;
            ws.ws_col = ser_cols;
        }
        if (fbcon_rows() > 0 && (!ser_rows || (unsigned)fbcon_rows() < ws.ws_row)) {
            ws.ws_row = (unsigned short)fbcon_rows();
            if (!ser_cols || (unsigned)fbcon_cols() < ws.ws_col)
                ws.ws_col = (unsigned short)fbcon_cols();
        }
        if (tty.rows && tty.cols) {
            ws.ws_row = tty.rows;
            ws.ws_col = tty.cols;
        }
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;
        return copy_to_user(arg, &ws, sizeof(ws));
    case TIOCSWINSZ:
        if (copy_from_user(&ws, arg, sizeof(ws)) < 0)
            return -EFAULT;
        if (ws.ws_row < 1 || ws.ws_col < 1)
            return -EINVAL;
        tty.rows = ws.ws_row;
        tty.cols = ws.ws_col;
        return 0;
    default:
        return -ENOTTY;
    }
}
