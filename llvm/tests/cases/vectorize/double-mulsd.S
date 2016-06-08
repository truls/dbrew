#include <test-common.h>
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    push rbp
    mov rbp, rsp

    pxor xmm0, xmm0
    pxor xmm1, xmm1

    movsd xmm0, [rdi]
    movsd xmm1, [rsi]
    mulsd xmm0, xmm1
    movsd [rdi], xmm0

    xor eax, eax

    movsd xmm0, [rdi+8]
    movsd xmm1, [rsi+8]
    mulsd xmm0, xmm1
    movsd [rdi+8], xmm0

    movsd xmm0, [rdi+16]
    movsd xmm1, [rsi+16]
    mulsd xmm0, xmm1
    movsd [rdi+16], xmm0

    movsd xmm0, [rdi+24]
    movsd xmm1, [rsi+24]
    mulsd xmm0, xmm1
    movsd [rdi+24], xmm0

    mov rsp, rbp
    pop rbp
    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_DOUBLE_ARRAY
    .quad 01122

