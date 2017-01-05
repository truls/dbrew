        .intel_syntax noprefix
        .text
        .globl  f1
        .type   f1, @function
f1:
        mov rsi, src
        mov rdi, dst
        mov rcx, 8
        rep movsb
        // See if we correctly resurrect static vars
        mov rsi, 0xAAAA
        mov rdi, 0xBBBB
        mov rcx, 8
        rep movsb
        ret

src:
        .quad 123456789
dst:
        .quad 0
