#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    mov rcx, [rip + index]
    mov rax, [rdi + 8 * rcx]
    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_INT_ARRAY
    .quad 0121

index:
    .quad 2

