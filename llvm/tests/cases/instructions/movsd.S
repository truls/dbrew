#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    movupd xmm0, [rdi]
    movupd xmm1, [rdi]
    movsd xmm0, [rdi]
    addpd xmm0, xmm1
    movupd [rdi], xmm0

    xor eax, eax

    ret

    .align 8
    .globl testCase
testCase:
    .quad 5
    .quad test
    .quad TEST_DRIVER_FLOAT_ARRAY
    .quad 128
    // Noalias params
    .quad 2
