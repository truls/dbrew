#include <test-common.h>
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    pxor xmm0, xmm0
    movq rax, xmm0
    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_INT_ARRAY
    .quad 020

addend:
    .quad 10

