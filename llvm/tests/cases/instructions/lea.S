#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    lea rdi, [rdi * 8]
    lea rax, [rdi * 4]
    mov rcx, 10
    lea rcx, [rcx * 8]
    add rax, rcx
    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_INT
    .quad 0221
