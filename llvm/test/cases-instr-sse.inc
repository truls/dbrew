#undef  TEST_NAMESPACE
#define TEST_NAMESPACE instr_sse

TEST_CASE("/instr/movd/togp", 0621, 0,
    movd eax, xmm0;
    ret;
)
TEST_CASE("/instr/movd/fromgp", 0221, 0,
    movd xmm0, edi;
    RET_XMM0;
)
TEST_CASE("/instr/movd/tomem", 0621, 0,
    sub rsp, 8;
    movd [rsp], xmm0;
    pop rax;
    mov eax, eax;
    ret;
)
TEST_CASE("/instr/movd/frommem", 0221, 0,
    push rdi;
    movd xmm0, [rsp];
    add rsp, 8;
    RET_XMM0;
)


TEST_CASE("/instr/movq/togp", 0721, 0,
    movq rax, xmm0;
    ret;
)
TEST_CASE("/instr/movq/fromgp", 0221, 0,
    movq xmm0, rdi;
    RET_XMM0;
)
TEST_CASE("/instr/movq/tomem", 0721, 0,
    sub rsp, 8;
    movq [rsp], xmm0;
    pop rax;
    ret;
)
TEST_CASE("/instr/movq/frommem", 0221, 0,
    push rdi;
    movq xmm0, [rsp];
    add rsp, 8;
    RET_XMM0;
)


TEST_CASE("/instr/movss/xmm", 02222, 0,
    UNPCK(xmm0, rdi);
    movd xmm1, esi;
    movss xmm0, xmm1;
    RET_XMM0;
)
TEST_CASE("/instr/movss/mem", 02222, 0,
    UNPCK(xmm0, rdi);
    push rsi;
    movss xmm0, [rsp];
    add rsp, 8;
    RET_XMM0;
)

TEST_CASE("/instr/movsd/xmm", 02222, 0,
    UNPCK(xmm0, rdi);
    movq xmm1, rsi;
    movsd xmm0, xmm1;
    RET_XMM0;
)
TEST_CASE("/instr/movsd/mem", 02222, 0,
    UNPCK(xmm0, rdi);
    push rsi;
    movsd xmm0, [rsp];
    add rsp, 8;
    RET_XMM0;
)

TEST_CASE("/instr/movaps", 0221, 0,
    UNPCK(xmm1, rdi);
    movaps xmm0, xmm1;
    RET_XMM0;
)
TEST_CASE("/instr/movapd", 0221, 0,
    UNPCK(xmm1, rdi);
    movapd xmm0, xmm1;
    RET_XMM0;
)
TEST_CASE("/instr/movdqa", 0221, 0,
    UNPCK(xmm1, rdi);
    movdqa xmm0, xmm1;
    RET_XMM0;
)

TEST_CASE("/instr/movlps/mem", 0221, 0,
    UNPCK(xmm0, rdi);
    sub rsp, 8;
    movlps [rsp], xmm0;
    pop rax;
    ret;
)
TEST_CASE("/instr/movlps/reg", 02222, 0,
    UNPCK(xmm0, rdi);
    push rsi;
    movlps xmm0, [rsp];
    add rsp, 8;
    RET_XMM0;
)
TEST_CASE("/instr/movlpd/mem", 0221, 0,
    UNPCK(xmm0, rdi);
    sub rsp, 8;
    movlpd [rsp], xmm0;
    pop rax;
    ret;
)
TEST_CASE("/instr/movlpd/reg", 02222, 0,
    UNPCK(xmm0, rdi);
    push rsi;
    movlpd xmm0, [rsp];
    add rsp, 8;
    RET_XMM0;
)

TEST_CASE("/instr/movhps/mem", 0221, 0,
    UNPCK(xmm0, rdi);
    sub rsp, 8;
    movhps [rsp], xmm0;
    pop rax;
    ret;
)
TEST_CASE("/instr/movhps/reg", 02222, 0,
    UNPCK(xmm0, rdi);
    push rsi;
    movhps xmm0, [rsp];
    add rsp, 8;
    RET_XMM0;
)
TEST_CASE("/instr/movhpd/mem", 0221, 0,
    UNPCK(xmm0, rdi);
    sub rsp, 8;
    movhpd [rsp], xmm0;
    pop rax;
    ret;
)
TEST_CASE("/instr/movhpd/reg", 02222, 0,
    UNPCK(xmm0, rdi);
    push rsi;
    movhpd xmm0, [rsp];
    add rsp, 8;
    RET_XMM0;
)

TEST_CASE("/instr/unpcklps", 07722, 0,
    unpcklps xmm0, xmm1;
    RET_XMM0;
)
TEST_CASE("/instr/unpcklpd", 07722, 0,
    unpcklpd xmm0, xmm1;
    RET_XMM0;
)


TEST_CASE("/instr/pxor/zero", 020, 0,
    pxor xmm0, xmm0;
    RET_XMM0;
)
