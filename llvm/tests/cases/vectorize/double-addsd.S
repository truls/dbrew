#include <test-common.h>
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    push rbp
    mov rbp, rsp

    movsd xmm0, [rdi]
    movsd xmm1, [rsi]
    addsd xmm0, xmm1
    movsd [rdi], xmm0

    xor eax, eax

    movsd xmm0, [rdi+8]
    movsd xmm1, [rsi+8]
    addsd xmm0, xmm1
    movsd [rdi+8], xmm0

    movsd xmm0, [rdi+16]
    movsd xmm1, [rsi+16]
    addsd xmm0, xmm1
    movsd [rdi+16], xmm0

    movsd xmm0, [rdi+24]
    movsd xmm1, [rsi+24]
    addsd xmm0, xmm1
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

