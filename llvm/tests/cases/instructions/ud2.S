#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function

    // Some gymnastics for Clang, because Clang always emits 6-byte jumps.
test:
    jmp 2f
    .align 16
1:
    ud2
2:
    xor eax, eax
    test rdi, rdi
    jz 1b
    inc eax
    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_INT_ARRAY
    .quad 0121
