#include <cpu.h>
#include <kern.h>
#include <proc.h>

static const char *const exc_name[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint", "Overflow",
    "BOUND Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
    "Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
    "Page Fault", "Reserved", "x87 FP Exception", "Alignment Check",
    "Machine Check", "SIMD FP Exception", "Virtualization Exception",
    "Control Protection", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Hypervisor Injection", "VMM Communication",
    "Security Exception", "Reserved"
};

static irq_handler_t irq_handlers[16];

static int exception_signal(uint64_t v)
{
    switch (v) {
    case 0:
    case 16:
    case 19:
        return 8;
    case 3:
        return 5;
    case 11:
    case 12:
    case 13:
    case 14:
        return 11;
    case 17:
        return 7;
    default:
        return 4;
    }
}

void irq_register(int irq, irq_handler_t fn)
{
    KASSERT(irq >= 0 && irq < 16);
    irq_handlers[irq] = fn;
    pic_unmask(irq);
}

static void dump_frame(struct trapframe *tf)
{
    uint64_t cr2, cr3;

    __asm__ volatile("movq %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("movq %%cr3, %0" : "=r"(cr3));

    kprintf("  RIP=%016lx  CS=%04lx  RFLAGS=%016lx\n", tf->rip, tf->cs, tf->rflags);
    kprintf("  RSP=%016lx  SS=%04lx  ERR=%016lx\n", tf->rsp, tf->ss, tf->err);
    kprintf("  RAX=%016lx  RBX=%016lx  RCX=%016lx\n", tf->rax, tf->rbx, tf->rcx);
    kprintf("  RDX=%016lx  RSI=%016lx  RDI=%016lx\n", tf->rdx, tf->rsi, tf->rdi);
    kprintf("  RBP=%016lx  R8 =%016lx  R9 =%016lx\n", tf->rbp, tf->r8, tf->r9);
    kprintf("  R10=%016lx  R11=%016lx  R12=%016lx\n", tf->r10, tf->r11, tf->r12);
    kprintf("  R13=%016lx  R14=%016lx  R15=%016lx\n", tf->r13, tf->r14, tf->r15);
    kprintf("  CR2=%016lx  CR3=%016lx\n", cr2, cr3);
}

void trap_dispatch(struct trapframe *tf)
{
    uint64_t v = tf->vector;

    if (v < 32) {
        if (tf->cs & 3) {
            uint64_t cr2;
            __asm__ volatile("movq %%cr2, %0" : "=r"(cr2));
            kprintf("[trap] pid %d (%s) killed: %s, rip=%lx cr2=%lx err=%lx\n",
                    curproc->pid, curproc->name, exc_name[v], tf->rip, cr2, tf->err);
            proc_kill_self(exception_signal(v));
        }
        if (v == 3) {
            kprintf("[trap] breakpoint at %016lx\n", tf->rip);
            return;
        }
        kprintf("\n*** CPU EXCEPTION %lu: %s\n", v, exc_name[v]);
        dump_frame(tf);
        panic("unhandled exception %lu", v);
    }

    if (v < 48) {
        int irq = (int)(v - 32);
        if (pic_is_spurious(irq))
            return;
        pic_eoi(irq);
        if (irq_handlers[irq])
            irq_handlers[irq](tf);
        return;
    }

    panic("unexpected vector %lu", v);
}
