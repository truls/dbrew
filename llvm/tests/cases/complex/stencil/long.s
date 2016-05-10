.intel_syntax noprefix
    .text
    .globl  test
    .type   test, @function
test:
    xor eax, eax
    mov r11, [rdi]
    test r11, r11
    jz 2f
    add rdi, 8
    mov r9d, 0
    mov eax, 0
1:
    mov r8, rcx
    add r8, [rdi + 0x8]
    mov r10, rdx
    add r10, [rdi]
    mov r8, [rsi + 8 * r8]
    mov r8, [r8 + 8 * r10]
    imul r8, [rdi + 0x10]
    add rax, r8
    add r9, 1
    add rdi, 0x18
    cmp r9, r11
    jne 1b
    ret
2:
    mov eax, 0
    ret

    .align 8
    .globl testCase
testCase:
    .quad 5
    .quad test
    .quad 3
    .quad 128
    .quad 3
    .quad stencilPtr

stencilPtr:
    .quad stencil

stencil:
    .quad 5
    .quad 0
    .quad 0
    .quad -4
    .quad -1
    .quad 0
    .quad 1
    .quad 1
    .quad 0
    .quad 1
    .quad 0
    .quad -1
    .quad 1
    .quad 0
    .quad 1
    .quad 1

