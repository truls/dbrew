#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    push rbp
    mov rbp, rsp

    mov rax, [rdi]
    mov rcx, [rip + addend]
    add rax, rcx
    mov [rdi], rax
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

addend:
    .quad 10

