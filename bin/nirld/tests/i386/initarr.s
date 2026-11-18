	.globl _start
	.text
fn:
	ret
_start:
	movl $__init_array_end, %ebx
	subl $__init_array_start, %ebx
	movl $1, %eax
	int $0x80
	.section .init_array,"aw"
	.long fn
	.long fn
