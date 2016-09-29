#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    movq xmm0, rdi
    movd eax, xmm0

    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_INT
    .quad 0221
