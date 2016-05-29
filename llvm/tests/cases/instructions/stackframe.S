#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl test
    .align 16
    .type test, @function
test:
    push rbp
    mov rbp, rsp
    mov [rbp - 8], rdi
    mov rax, [rbp - 8]
    add rax, 10
    pop rbp
    ret

    .align 8
    .globl testCase
testCase:
    .quad 3
    .quad test
    .quad TEST_DRIVER_INT
