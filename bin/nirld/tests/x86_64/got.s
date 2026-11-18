	.globl _start
	.text
_start:
	movq counter@GOTPCREL(%rip), %rax
	movl (%rax), %edi
	movl $60, %eax
	syscall
	.data
counter:
	.long 5
