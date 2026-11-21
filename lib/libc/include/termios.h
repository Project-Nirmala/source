#ifndef _LIBC_TERMIOS_H
#define _LIBC_TERMIOS_H

#include <sys/termios.h>

int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int action, const struct termios *t);
speed_t cfgetospeed(const struct termios *t);
speed_t cfgetispeed(const struct termios *t);
int cfsetospeed(struct termios *t, speed_t s);
int cfsetispeed(struct termios *t, speed_t s);

#endif
