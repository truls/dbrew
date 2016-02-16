        .text
	.globl	f1
	.type	f1, @function
f1:
	pxor	%xmm1, %xmm2
	movsd 0x60e0c8,%xmm0
	ret
