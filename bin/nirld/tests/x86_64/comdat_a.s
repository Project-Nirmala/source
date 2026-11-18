	.globl _start
	.section .text.dup,"axG",@progbits,dup,comdat
	.globl dup
dup:
	movl $1, %eax
	ret
	.text
_start:
	call dup
	movl %eax, %edi
	call other
	addl %eax, %edi
	movl $60, %eax
	syscall
