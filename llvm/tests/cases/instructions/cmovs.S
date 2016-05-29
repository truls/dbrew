#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    xor eax, eax
    test edi, edi
    cmovns eax, edi
    neg eax

    ret

    .align 8
    .globl testCase
testCase:
    .quad 5
    .quad test
    .quad TEST_DRIVER_INT
    .quad 128
    // Noalias params
    .quad 2
