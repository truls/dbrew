#include "test-common.h"
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    // Ugly variant of rdi = rdi * 32
    add rdi, rdi
    add rdi, rdi
    add rdi, rdi
    add rdi, rdi
    add rdi, rdi
    movzx eax, dil

    ret

    .align 8
    .globl testCase
testCase:
    .quad 3
    .quad test
    .quad TEST_DRIVER_INT
