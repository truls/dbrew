#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    add rdi, 0
    test rdi, rdi
    jz 1f

    dec rdi
    call test
    inc rax

    ret
1:
    xor eax, eax
    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_INT
    .quad 0221
