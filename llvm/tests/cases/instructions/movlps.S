#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    movlps xmm1, [rdi]
    movups xmm0, [rsi]
    movups [rdi], xmm0
    movlps [rdi], xmm1

    xor eax, eax

    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_FLOAT_ARRAY
    .quad 01122
