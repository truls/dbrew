//!driver = test-driver-decode.c
.intel_syntax noprefix
    .text
    .globl  f1
    .type   f1, @function
f1:
    // PUSH instructions, Intel Vol. 2B 4-272
    push word ptr [rdx]
    push qword ptr [rdx]
    push dx
    push rdx
    push 0x10
    push 0x1000
    push 0x10000000
    // push fs
    // push gs

    // POP instructions, Intel Vol. 2B 4-183
    pop word ptr [rdx]
    pop qword ptr [rdx]
    pop dx
    pop rdx

    ret
