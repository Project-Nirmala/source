	.globl _start
	.text
_start:
	xorl %ebp, %ebp
	call tls_setup
	call main
	movl %eax, %ebx
	movl $1, %eax
	int $0x80
