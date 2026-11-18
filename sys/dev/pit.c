#include <cpu.h>
#include <io.h>
#include <proc.h>

#define PIT_BASE_HZ 1193182u

static volatile uint64_t ticks;
static uint32_t freq;

static void pit_irq(struct trapframe *tf)
{
    ticks++;
    sched_tick(tf);
}

void pit_init(uint32_t hz)
{
    uint32_t div = PIT_BASE_HZ / hz;

    if (div > 65535) div = 65535;
    if (div == 0)    div = 1;
    freq = PIT_BASE_HZ / div;

    outb(0x43, 0x34);
    outb(0x40, div & 0xFF);
    outb(0x40, (div >> 8) & 0xFF);

    irq_register(0, pit_irq);
}

uint64_t pit_ticks(void) { return ticks; }
uint32_t pit_hz(void)    { return freq; }
