	.globl _start
	.text
_start:
	call 1f
1:	popl %ebx
	addl $_GLOBAL_OFFSET_TABLE_+(.-1b), %ebx
	movl counter@GOT(%ebx), %ecx
	movl (%ecx), %edx
	leal counter@GOTOFF(%ebx), %esi
	cmpl %ecx, %esi
	jne bad
	movl %edx, %ebx
	movl $1, %eax
	int $0x80
bad:
	movl $99, %ebx
	movl $1, %eax
	int $0x80
	.data
counter:
	.long 5
