	.globl _start
	.text
_start:
	xorl %ebp, %ebp
	call main
	movl %eax, %ebx
	movl $1, %eax
	int $0x80
