#include <io.h>
#include <serial.h>
#include <tty.h>
#include <cpu.h>
#include <fbcon.h>
#include <types.h>

#define COM1 0x3F8

void serial_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void serial_putc(char c)
{
    if (c == '\n')
        serial_putc('\r');
    while ((inb(COM1 + 5) & 0x20) == 0)
        ;
    outb(COM1, (uint8_t)c);
    fbcon_putc(c);
}

static void serial_raw_putc(char c)
{
    while ((inb(COM1 + 5) & 0x20) == 0)
        ;
    outb(COM1, (uint8_t)c);
}

static int probe_state;
static uint64_t probe_deadline;
static unsigned probe_rows, probe_cols;

void serial_probe_size(void)
{
    static const char q[] = "\033[s\033[999;999H\033[6n\033[u";
    size_t i;

    probe_state = 1;
    probe_deadline = pit_ticks() + 100;
    for (i = 0; q[i]; i++)
        serial_raw_putc(q[i]);
}

static bool probe_feed(uint8_t c)
{
    switch (probe_state) {
    case 1:
        if (c == 0x1B) {
            probe_state = 2;
            return true;
        }
        return false;
    case 2:
        if (c == '[') {
            probe_state = 3;
            probe_rows = probe_cols = 0;
            return true;
        }
        probe_state = 0;
        tty_input(0x1B);
        return false;
    case 3:
        if (c >= '0' && c <= '9') {
            probe_rows = probe_rows * 10 + (c - '0');
            return true;
        }
        if (c == ';') {
            probe_state = 4;
            return true;
        }
        break;
    case 4:
        if (c >= '0' && c <= '9') {
            probe_cols = probe_cols * 10 + (c - '0');
            return true;
        }
        if (c == 'R') {
            probe_state = 0;
            if (probe_rows && probe_cols)
                tty_set_winsize((unsigned short)probe_rows, (unsigned short)probe_cols);
            return true;
        }
        break;
    }
    probe_state = 0;
    return true;
}

static void serial_rx_char(uint8_t c)
{
    if (probe_state && pit_ticks() > probe_deadline)
        probe_state = 0;
    if (probe_state && probe_feed(c))
        return;
    tty_input(c);
}

static void serial_irq(struct trapframe *tf)
{
    (void)tf;
    while (inb(COM1 + 5) & 1)
        serial_rx_char(inb(COM1));
}

void serial_enable_rx(void)
{
    while (inb(COM1 + 5) & 1)
        inb(COM1);
    outb(COM1 + 1, 0x01);
    irq_register(4, serial_irq);
}
