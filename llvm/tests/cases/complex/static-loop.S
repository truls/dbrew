#include <test-common.h>
    .globl  test
    .type   test, @function
test:
.LFB2:
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $16, %rsp
    movl    $0, -12(%rbp)
    jmp .L5
.L6:
    movl    $0, %eax
    addl    $1, -12(%rbp)
.L5:
    cmpl    $1, -12(%rbp)
    jle .L6
    movl    -12(%rbp), %eax
    nop
    leave
    ret
.LFE2:
    .size   test, .-test

    .align 8
    .globl testCase
testCase:
    .quad 4
    .quad test
    .quad TEST_DRIVER_INT
    .quad 0221
