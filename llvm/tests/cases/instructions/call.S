#include "test-common.h"
.intel_syntax noprefix
    .text
    .extern test_add

    .globl test
    .align 16
    .type test, @function
test:
    push rdi
    call test_add
    pop rdi
    add rax, rdi
    ret

    .align 8
    .globl testCase
testCase:
    .quad 3
    .quad test
    .quad TEST_DRIVER_INT
