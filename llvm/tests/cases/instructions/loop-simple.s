.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    push rbp
    mov rbp, rsp
    mov rax, rdi

1:
    sub rax, 1
    cmp rax, 0
    jne 1b

    leave
    ret

    .align 8
    .globl testCase
testCase:
    .quad 3
    .quad test
    .quad 2

