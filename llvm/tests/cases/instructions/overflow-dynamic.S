#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    movabs rcx, 0x8000000000000000
    xor rax, rax
    sub rdi, rcx
    cmovno rax, rdi
    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_INT
    .quad 0221

addend:
    .quad 10

