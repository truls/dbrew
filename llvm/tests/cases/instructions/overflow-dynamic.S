#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    mov rcx, 0x8000000000000000
    xor rax, rax
    sub rdi, rcx
    cmovno rax, rdi
    ret

    .align 8
    .globl testCase
testCase:
    .quad 3
    .quad test
    .quad TEST_DRIVER_INT

addend:
    .quad 10

