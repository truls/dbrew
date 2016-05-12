//!driver = test-driver-decode.c
.intel_syntax noprefix
    .text
    .globl  f1
    .type   f1, @function
f1:
    test al, r9b
    test r10b, bl
    test cl, dl
//FIXME    test ah, bh
//FIXME    test sil, dil

    test eax, r9d
    test r10d, ebx
    test rax, r9
    test r10, rbx
    ret
