.section .text.start
.global _start
_start:
    xorl %ebp, %ebp
    movq (%rsp), %rdi
    leaq 8(%rsp), %rsi
    leaq 16(%rsp,%rdi,8), %rdx
    movq %rdx, environ(%rip)
    andq $-16, %rsp
    call main
    movl %eax, %edi
    call exit
    ud2
