	.section .text.dup,"axG",@progbits,dup,comdat
	.globl dup
dup:
	movl $2, %eax
	ret
	.text
	.globl other
other:
	call dup
	ret
