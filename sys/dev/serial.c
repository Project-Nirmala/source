#include <io.h>
#include <serial.h>
#include <tty.h>
#include <cpu.h>
#include <fbcon.h>

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

static void serial_rx_char(uint8_t c)
{
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
