#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    push rbp
    mov rbp, rsp

    mov rax, 1
    cmp rdi, 0
    cmove rax, rsi

    leave
    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_INT_ARRAY
    .quad 00022

addend:
    .quad 10

