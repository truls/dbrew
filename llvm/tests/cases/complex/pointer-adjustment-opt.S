#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    mov ecx, 8
1:
    add qword ptr [rdi], 10
    add rdi, 8
    dec ecx
    test ecx, ecx
    jnz 1b
    xor eax, eax
    ret

    .data
    .align 8
    .globl testCase
testCase:
    .quad 7
    .quad test
    .quad TEST_DRIVER_INT_ARRAY
    .quad 0121
    .quad 128
    .quad 0
    .quad 1 // Enable unsafe pointer opts
