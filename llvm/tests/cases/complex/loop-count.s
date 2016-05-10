.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    push rbp
    mov rbp, rsp

    xor eax, eax
    jmp 2f

1:
    add rax, 1

2:
    cmp rax, rdi
    jl 1b

    pop rbp
    ret

    .align 8
    .globl testCase
testCase:
    .quad 3
    .quad test
    .quad 2

