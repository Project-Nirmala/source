	.globl puts
	.text
puts:
	movq %rdi, %rsi
	xorl %edx, %edx
1:	cmpb $0, (%rsi,%rdx)
	je 2f
	incq %rdx
	jmp 1b
2:	movl $1, %eax
	movl $1, %edi
	syscall
	movl $1, %eax
	movl $1, %edi
	leaq nl(%rip), %rsi
	movl $1, %edx
	syscall
	xorl %eax, %eax
	ret
	.section .rodata
nl:	.byte 10
