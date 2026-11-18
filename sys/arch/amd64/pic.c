#include <cpu.h>
#include <io.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

void pic_init(void)
{
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, 32);  io_wait();
    outb(PIC2_DATA, 40);  io_wait();
    outb(PIC1_DATA, 4);   io_wait();
    outb(PIC2_DATA, 2);   io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, 0xFB);
    outb(PIC2_DATA, 0xFF);
}

void pic_mask(int irq)
{
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    outb(port, inb(port) | (1 << (irq & 7)));
}

void pic_unmask(int irq)
{
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    outb(port, inb(port) & ~(1 << (irq & 7)));
}

void pic_eoi(int irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

bool pic_is_spurious(int irq)
{
    uint8_t isr;

    if (irq != 7 && irq != 15)
        return false;
    if (irq == 7) {
        outb(PIC1_CMD, 0x0B);
        isr = inb(PIC1_CMD);
        return !(isr & 0x80);
    }
    outb(PIC2_CMD, 0x0B);
    isr = inb(PIC2_CMD);
    if (!(isr & 0x80)) {
        outb(PIC1_CMD, PIC_EOI);
        return true;
    }
    return false;
}
