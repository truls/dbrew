#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    movups xmm0, [rdi]
    movups xmm1, [rdi]
    movss xmm0, [rdi]
    addps xmm0, xmm1
    movups [rdi], xmm0

    xor eax, eax

    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_FLOAT_ARRAY
    .quad 01122
