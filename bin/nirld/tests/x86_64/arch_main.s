	.globl _start
	.text
_start:
	call getval
	movl %eax, %edi
	movl $60, %eax
	syscall
