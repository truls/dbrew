#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    sub rsp, 8
    mov [rsp], rdi
    mov rax, [rsp]
    push rdi
    push rdi
    push rdi
    push rsi
    mov [rsp], rdi
    pop rax
    pop rcx
    pop rcx
    pop rcx
    add rsp, 8
    ret

    .data
    .align 8
    .globl testCase
testCase:
    .quad 7
    .quad test
    .quad TEST_DRIVER_INT
    .quad 0221
    .quad 128
    .quad 0
    .quad 1 // Enable unsafe pointer opts
