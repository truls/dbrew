#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    xor eax, eax
    test rdi, rdi
    jz 1f
    inc eax
1:
    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DBREW_INT
    .quad 0221
