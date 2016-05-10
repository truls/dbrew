.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    push rbp
    mov rbp, rsp

    mov rax, 1
    cmp rdi, 0
    cmove rax, rsi

    leave
    ret

    .align 8
    .globl testCase
testCase:
    .quad 3
    .quad test
    .quad 0

addend:
    .quad 10

