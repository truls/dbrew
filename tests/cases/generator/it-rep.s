//!driver = test-driver-gen.c
.intel_syntax noprefix
    .text
    .globl  f1
    .type   f1, @function
f1:
        rep movsb
        movsb
        rep movsw
        movsw
        rep movsd
        movsd
        rep movsq
        movsq
        ret
