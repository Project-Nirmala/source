	.globl add3
	.text
add3:
	leal 3(%rdi), %eax
	ret
	.comm shared,4,4
