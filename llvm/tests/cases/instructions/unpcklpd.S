#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    movsd xmm0, [rdi]
    movsd xmm1, [rsi]
    unpcklpd xmm0, xmm1
    movupd [rdi], xmm0

    xor eax, eax

    ret

    .data
    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_DOUBLE_ARRAY
    .quad 01122
