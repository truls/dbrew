#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    xor eax, eax
    mov eax, 0x20000
    mov ecx, 0x10000
    mov cx, di
    mov ch, cl
    add ch, 1
    mov cl, ch
    mov ax, cx
    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_INT
    .quad 0221
