#ifndef _SYS_TERMIOS_H
#define _SYS_TERMIOS_H

typedef unsigned int  tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int  speed_t;

#define NCCS 19

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_line;
    cc_t     c_cc[NCCS];
};

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define VINTR  0
#define VQUIT  1
#define VERASE 2
#define VKILL  3
#define VEOF   4
#define VTIME  5
#define VMIN   6

#define ISIG    0x01
#define ICANON  0x02
#define ECHO    0x08
#define ECHOE   0x10
#define ECHOK   0x20

#define ECHONL  0x40
#define NOFLSH  0x80
#define TOSTOP  0x100
#define IEXTEN  0x8000

#define IGNBRK  0x01
#define BRKINT  0x02
#define IGNPAR  0x04
#define PARMRK  0x08
#define INPCK   0x10
#define ISTRIP  0x20
#define INLCR   0x40
#define IGNCR   0x80
#define ICRNL   0x100
#define IXON    0x400
#define IXANY   0x800
#define IXOFF   0x1000

#define OPOST   0x01
#define ONLCR   0x04

#define CSIZE   0x30
#define CS5     0x00
#define CS6     0x10
#define CS7     0x20
#define CS8     0x30
#define CSTOPB  0x40
#define PARENB  0x100
#define PARODD  0x200
#define HUPCL   0x400
#define CLOCAL  0x800
#define CREAD   0x80
#define B0      0
#define B9600   0x0D
#define B38400  0x0F
#define B115200 0x1002

#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TIOCGPGRP   0x540F
#define TIOCSPGRP   0x5410
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414
#define FIONREAD    0x541B

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

#endif
