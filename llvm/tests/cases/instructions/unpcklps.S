#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    movlps xmm0, [rdi]
    movlps xmm1, [rsi]
    unpcklps xmm0, xmm1
    movups [rdi], xmm0

    xor eax, eax

    ret

    .align 8
    .globl testCase
testCase:
    .quad 5
    .quad test
    .quad TEST_DRIVER_FLOAT_ARRAY
    .quad 128
    // Noalias params
    .quad 2
