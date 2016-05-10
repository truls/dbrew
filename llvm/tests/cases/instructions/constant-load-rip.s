.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    push rbp
    mov rbp, rsp

    mov rax, [rdi]
    mov rcx, [rip + addend]
    add rax, rcx
    mov [rdi], rax
    xor eax, eax

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

