#include <test-common.h>
.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    test rdi, rdi
    jz 1f

    push rdi
    sub rdi, 1
    call test
    pop rdi
    add rax, rdi
    ret

1:
    xor eax, eax
    ret

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_INT
    .quad 0221
