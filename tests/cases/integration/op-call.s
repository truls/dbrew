    .text
	.globl	f1
	.type	f1, @function
f1:
	mov $jtarget, %r14
	call %r14
	ret
jtarget:
	mov $0, %eax
	ret
