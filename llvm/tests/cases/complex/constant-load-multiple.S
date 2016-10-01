#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    push rbp
    mov rbp, rsp

    mov rcx, [rip + addend1]
    add [rdi], rcx
    mov rcx, [rip + addend2]
    add [rdi + 8], rcx
    xor eax, eax

    leave
    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_INT_ARRAY
    .quad 0121

addend1:
    .quad 10
addend2:
    .quad 20

