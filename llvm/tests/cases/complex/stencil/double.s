.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    xor eax, eax
    mov r10, [rdi]
    test r10, r10
    jz 2f
    pxor xmm0, xmm0
    add rdi, 8
1:
    mov r8, rcx
    add r8, [rdi + 8]
    mov r9, rdx
    add r9, [rdi]
    mov r8, [rsi + 8 * r8]
    movsd xmm1, [r8 + 8 * r9]
    mulsd xmm1, [rdi + 0x10]
    addsd xmm0, xmm1
    add rdi, 0x18
    inc rax
    cmp rax, r10
    jne 1b
    movq rax, xmm0
    ret
2:
    ret

    .align 8
    .globl testCase
testCase:
    .quad 6
    .quad test
    .quad 4
    .quad 128
    .quad 3
    .quad stencilPtr

stencilPtr:
    .quad stencil

stencil:
    .quad 5
    .quad 0
    .quad 0
    .double -0.2
    .quad -1
    .quad 0
    .double 0.3
    .quad 1
    .quad 0
    .double 0.3
    .quad 0
    .quad -1
    .double 0.3
    .quad 0
    .quad 1
    .double 0.3

