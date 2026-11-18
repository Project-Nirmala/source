	.globl _start
	.weak weakfn
	.text
_start:
	movl $10, %edi
	call add3
	movl %eax, %edi
	addl counter(%rip), %edi
	movq ptr(%rip), %rax
	addl (%rax), %edi
	movl $str, %esi
	movzbl (%rsi), %eax
	subl $64, %eax
	addl %eax, %edi
	addl shared(%rip), %edi
	movl $weakfn, %eax
	testl %eax, %eax
	jnz bad
	addl $1, %edi
	movl $60, %eax
	syscall
bad:
	movl $99, %edi
	movl $60, %eax
	syscall
	.data
counter:
	.long 5
ptr:
	.quad counter
	.section .rodata
str:
	.asciz "A"
	.comm shared,4,4
