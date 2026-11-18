#include <cpu.h>
#include <kern.h>

struct idt_entry {
    uint16_t off_lo;
    uint16_t sel;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t off_mid;
    uint32_t off_hi;
    uint32_t zero;
} __packed;

struct idtr {
    uint16_t limit;
    uint64_t base;
} __packed;

#define IDT_VECTORS 48
#define GATE_INT    0x8E

static struct idt_entry idt[256] __attribute__((aligned(16)));
extern uint64_t isr_stub_table[];

static void set_gate(int v, uint64_t handler, uint8_t ist)
{
    idt[v].off_lo    = handler & 0xFFFF;
    idt[v].sel       = SEL_KCODE;
    idt[v].ist       = ist;
    idt[v].type_attr = GATE_INT;
    idt[v].off_mid   = (handler >> 16) & 0xFFFF;
    idt[v].off_hi    = (uint32_t)(handler >> 32);
    idt[v].zero      = 0;
}

void idt_init(void)
{
    struct idtr r;
    int i;

    memset(idt, 0, sizeof(idt));
    for (i = 0; i < IDT_VECTORS; i++)
        set_gate(i, isr_stub_table[i], i == 8 ? 1 : 0);

    r.limit = sizeof(idt) - 1;
    r.base  = (uint64_t)idt;
    __asm__ volatile("lidt %0" : : "m"(r));
}
