	.globl _start
	.text
_start:
	xorl %ebp, %ebp
	call tls_setup
	call main
	movl %eax, %edi
	movl $60, %eax
	syscall
