	.globl _start
	.text
fn:
	ret
_start:
	movl $__init_array_end, %edi
	subl $__init_array_start, %edi
	movl $60, %eax
	syscall
	.section .init_array,"aw"
	.quad fn
	.quad fn
