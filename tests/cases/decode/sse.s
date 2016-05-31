//!driver = test-driver-decode.c
.intel_syntax noprefix
    .text
    .globl  f1
    .type   f1, @function
f1:
    movq rax, xmm0
    movq rsi, xmm7
    movq rax, xmm15
    movq r15, xmm15
    movq xmm0, rax
    movq xmm7, r9
    movq xmm15, rax
    movq xmm15, r15
    movss xmm0, [rdi]
    movsd xmm0, [rdi]
    movups xmm0, [rdi]
    movupd xmm0, [rdi]
    movaps xmm0, [rdi]
    movapd xmm0, [rdi]
    movss [rdi], xmm0
    movsd [rdi], xmm0
    movups [rdi], xmm0
    movupd [rdi], xmm0
    movaps [rdi], xmm0
    movapd [rdi], xmm0
    movq xmm0, xmm1
    movq [rdx], xmm1
    movdqu xmm0, [rdx]
    movdqu [rdx], xmm0
    movdqa xmm0, [rdx]
    movdqa [rdx], xmm0
    movd xmm0, [rdx]
    movd [rdx], xmm0
    movlpd [rsi], xmm10
    movlpd xmm0, [rdi]
    movlps [rsi], xmm10
    movlps xmm0, [rdi]
    movhpd [rsi], xmm10
    movhpd xmm0, [rdi]
    movhps [rsi], xmm10
    movhps xmm0, [rdi]

    unpcklpd xmm0, xmm1
    unpcklpd xmm0, [rdi]
    unpcklps xmm0, xmm1
    unpcklps xmm0, [rdi]
    unpckhpd xmm0, xmm1
    unpckhpd xmm0, [rdi]
    unpckhps xmm0, xmm1
    unpckhps xmm0, [rdi]

    addss xmm0, xmm1
    addsd xmm0, xmm1
    addps xmm0, xmm1
    addpd xmm0, xmm1
    addss xmm0, [rax]
    addsd xmm0, [rax]
    addps xmm0, [rax]
    addpd xmm0, [rax]
    subss xmm0, xmm1
    subsd xmm0, xmm1
    subps xmm0, xmm1
    subpd xmm0, xmm1
    subss xmm0, [rax]
    subsd xmm0, [rax]
    subps xmm0, [rax]
    subpd xmm0, [rax]
    mulss xmm0, xmm1
    mulsd xmm0, xmm1
    mulps xmm0, xmm1
    mulpd xmm0, xmm1
    mulss xmm0, [rax]
    mulsd xmm0, [rax]
    mulps xmm0, [rax]
    mulpd xmm0, [rax]

    paddq xmm0, xmm1
    paddq xmm0, [rax]

    ret

