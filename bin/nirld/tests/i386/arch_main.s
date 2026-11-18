	.globl _start
	.text
_start:
	call getval
	movl %eax, %ebx
	movl $1, %eax
	int $0x80
