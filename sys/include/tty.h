#ifndef _NIRMALA_TTY_H
#define _NIRMALA_TTY_H

#include <types.h>

void tty_init(void);
void tty_input(uint8_t c);
long tty_read(void *buf, size_t n);
long tty_write(const void *buf, size_t n);
long tty_ioctl(uint64_t req, uint64_t arg);

void kbd_init(void);
void serial_enable_rx(void);

#endif
