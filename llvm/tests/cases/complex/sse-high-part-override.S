#include <test-common.h>
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    movupd xmm0, [rdi]
    movupd xmm1, [rsi]
    addsd xmm0, xmm1
    movapd [rdi], xmm0
    movsd xmm0, [rdi]
    movsd xmm1, [rsi]
    addsd xmm0, xmm1
    movsd [rdi], xmm0

    xor eax, eax

    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_DOUBLE_ARRAY
    .quad 01122
