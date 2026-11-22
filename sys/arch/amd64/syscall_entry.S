#include <cpu.h>

.section .text
.global syscall_entry
syscall_entry:
    movq %rsp, cpu0+16(%rip)
    movq cpu0+8(%rip), %rsp
    pushq $SEL_UDATA
    pushq cpu0+16(%rip)
    pushq %r11
    pushq $SEL_UCODE
    pushq %rcx
    pushq $0
    pushq $256
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    cld
    call syscall_dispatch
    jmp isr_return
