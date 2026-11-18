	.globl _start
	.weak weakfn
	.text
_start:
	movl $10, %ebx
	pushl %ebx
	call add3
	addl $4, %esp
	movl %eax, %ebx
	addl counter, %ebx
	movl ptr, %eax
	addl (%eax), %ebx
	movl $str, %esi
	movzbl (%esi), %eax
	subl $64, %eax
	addl %eax, %ebx
	addl shared, %ebx
	movl $weakfn, %eax
	testl %eax, %eax
	jnz bad
	addl $1, %ebx
	movl $1, %eax
	int $0x80
bad:
	movl $99, %ebx
	movl $1, %eax
	int $0x80
	.data
counter:
	.long 5
ptr:
	.long counter
	.section .rodata
str:
	.asciz "A"
	.comm shared,4,4
