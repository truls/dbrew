#include "test-common.h"
.intel_syntax noprefix
    .text
    .extern test_add

    .globl test
    .align 16
    .type test, @function
test:
    push rdi
    lea rax, test_add
    call rax
    pop rdi
    add rax, rdi
    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_INT
    .quad 0221
