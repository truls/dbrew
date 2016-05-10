.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    push rbp
    mov rbp, rsp

    xor rax, rax
    mov edx, 1
    test rdi, rdi
    cmovz rdx, rax

    // Equivalent to ret rdx
    mov rcx, 0x7fffffffffffffff
    add rcx, rdx
    jno 1f
    inc rax

1:
    leave
    ret

    .align 8
    .globl testCase
testCase:
    .quad 3
    .quad test
    .quad 2

addend:
    .quad 10

