	.globl add3
	.text
add3:
	movl 4(%esp), %eax
	addl $3, %eax
	ret
	.comm shared,4,4
