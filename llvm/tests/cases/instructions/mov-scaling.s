.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    mov rcx, [index]
    mov rax, [rdi + 8 * rcx]
    ret

    .align 8
    .globl testCase
testCase:
    .quad 3
    .quad test
    .quad 0

index:
    .quad 2

