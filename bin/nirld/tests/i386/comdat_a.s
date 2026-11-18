	.globl _start
	.section .text.dup,"axG",@progbits,dup,comdat
	.globl dup
dup:
	movl $1, %eax
	ret
	.text
_start:
	call dup
	movl %eax, %ebx
	call other
	addl %eax, %ebx
	movl $1, %eax
	int $0x80
