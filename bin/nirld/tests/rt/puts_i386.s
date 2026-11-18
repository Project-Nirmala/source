	.globl puts
	.text
puts:
	movl 4(%esp), %ecx
	xorl %edx, %edx
1:	cmpb $0, (%ecx,%edx)
	je 2f
	incl %edx
	jmp 1b
2:	movl $4, %eax
	movl $1, %ebx
	int $0x80
	movl $4, %eax
	movl $1, %ebx
	movl $nl, %ecx
	movl $1, %edx
	int $0x80
	xorl %eax, %eax
	ret
	.section .rodata
nl:	.byte 10
