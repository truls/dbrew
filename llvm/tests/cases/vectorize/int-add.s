.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    push rbp
    mov rbp, rsp

    mov rax, [rdi]
    mov [rsp - 8], rax
    add qword ptr [rdi], 0xa
    add qword ptr [rdi + 8], 0xa
    add qword ptr [rdi + 16], 0xa
    add qword ptr [rdi + 24], 0xa
    add qword ptr [rdi + 32], 0xa
    add qword ptr [rdi + 40], 0xa
    add qword ptr [rdi + 48], 0xa
    add qword ptr [rdi + 56], 0xa
    mov rax, [rsp - 8]

    mov rsp, rbp
    pop rbp
    ret

    .align 8
    .globl testCase
testCase:
    .quad 3
    .quad test
    .quad 0

