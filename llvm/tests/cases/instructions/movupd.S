#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    movupd xmm0, [rsi]
    movupd [rdi], xmm0

    xor eax, eax

    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_DOUBLE_ARRAY
    .quad 01122
