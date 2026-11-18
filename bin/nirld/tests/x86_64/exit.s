	.globl _start
	.text
_start:
	movl $60, %eax
	movl $42, %edi
	syscall
