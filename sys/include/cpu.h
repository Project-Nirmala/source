#ifndef _NIRMALA_CPU_H
#define _NIRMALA_CPU_H

#define SEL_KCODE 0x08
#define SEL_KDATA 0x10
#define SEL_UDATA (0x18 | 3)
#define SEL_UCODE (0x20 | 3)
#define SEL_TSS   0x28

#ifndef __ASSEMBLER__
#include <types.h>

struct trapframe {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, err;
    uint64_t rip, cs, rflags, rsp, ss;
};

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);

void idt_init(void);
void trap_dispatch(struct trapframe *tf);

typedef void (*irq_handler_t)(struct trapframe *tf);
void irq_register(int irq, irq_handler_t fn);

void pic_init(void);
void pic_eoi(int irq);
void pic_mask(int irq);
void pic_unmask(int irq);
bool pic_is_spurious(int irq);

void lapic_init(void);

void     pit_init(uint32_t hz);
uint64_t pit_ticks(void);
uint32_t pit_hz(void);

#endif
#endif
