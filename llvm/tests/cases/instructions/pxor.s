.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    pxor xmm0, xmm0
    movq rax, xmm0
    ret

    .align 8
    .globl testCase
testCase:
    .quad 3
    .quad test
    .quad 0

addend:
    .quad 10

