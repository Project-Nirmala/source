#ifndef _LIBC_TERMIOS_H
#define _LIBC_TERMIOS_H

#include <sys/termios.h>

int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int action, const struct termios *t);

#endif
