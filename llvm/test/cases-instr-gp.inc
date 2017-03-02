#undef  TEST_NAMESPACE
#define TEST_NAMESPACE instr_gp

TEST_CASE("/instr/movsx/64-8", 0221, 0,
    movsx rax, dil;
    ret;
)
TEST_CASE("/instr/movsx/64-16", 0221, 0,
    movsx rax, di;
    ret;
)
TEST_CASE("/instr/movsx/64-32", 0221, 0,
    movsxd rax, edi;
    ret;
)
TEST_CASE("/instr/movsx/32-8", 0221, 0,
    movsx eax, dil;
    ret;
)
TEST_CASE("/instr/movsx/32-16", 0221, 0,
    movsx eax, di;
    ret;
)
TEST_CASE("/instr/movsx/16-8", 0221, 0,
    xor eax, eax;
    movsx ax, dil;
    ret;
)
TEST_CASE("/instr/cdqe", 0221, 0,
    mov eax, edi;
    cdqe;
    ret;
)

TEST_CASE("/instr/movzx/64-8", 0221, 0,
    movzx rax, dil;
    ret;
)
TEST_CASE("/instr/movzx/64-16", 0221, 0,
    movzx rax, di;
    ret;
)
TEST_CASE("/instr/movzx/32-8", 0221, 0,
    movzx eax, dil;
    ret;
)
TEST_CASE("/instr/movzx/32-16", 0221, 0,
    movzx eax, di;
    ret;
)
TEST_CASE("/instr/movzx/16-8", 0221, 0,
    xor eax, eax;
    movzx ax, dil;
    ret;
)


TEST_CASE("/instr/neg/64", 0221, 0,
    neg rdi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/neg/32", 0221, 0,
    neg edi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/neg/16", 0221, 0,
    neg di;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/neg/8", 0221, 0,
    neg dil;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/neg/flags/rand", 0221, 0,
    neg rdi;
    RET_FLAGS;
)
TEST_CASE("/instr/neg/flags/zero", 020, 0,
    xor edi, edi;
    neg rdi;
    RET_FLAGS;
)
TEST_CASE("/instr/neg/flags/min", 020, 0,
    mov edi, 1 << 31;
    neg edi;
    RET_FLAGS;
)


TEST_CASE("/instr/not/64", 0221, 0,
    not rdi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/not/32", 0221, 0,
    not edi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/not/16", 0221, 0,
    not di;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/not/8", 0221, 0,
    not dil;
    mov rax, rdi;
    ret;
)


TEST_CASE("/instr/inc/64", 0221, 0,
    inc rdi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/inc/32", 0221, 0,
    inc edi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/inc/16", 0221, 0,
    inc di;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/inc/8", 0221, 0,
    inc dil;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/inc/flags/rand", 0221, 0,
    xor eax, eax; // Define all flags
    inc rdi;
    RET_FLAGS;
)
TEST_CASE("/instr/inc/flags/zero", 020, 0,
    xor edi, edi;
    inc rdi;
    RET_FLAGS;
)
TEST_CASE("/instr/inc/flags/max", 020, 0,
    xor eax, eax; // Define all flags
    mov edi, 0x7fffffff;
    inc edi;
    RET_FLAGS;
)


TEST_CASE("/instr/dec/64", 0221, 0,
    dec rdi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/dec/32", 0221, 0,
    dec edi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/dec/16", 0221, 0,
    dec di;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/dec/8", 0221, 0,
    dec dil;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/dec/flags/rand", 0221, 0,
    xor eax, eax; // Define all flags
    dec rdi;
    RET_FLAGS;
)
TEST_CASE("/instr/dec/flags/zero", 020, 0,
    xor edi, edi;
    dec rdi;
    RET_FLAGS;
)
TEST_CASE("/instr/dec/flags/min", 020, 0,
    xor eax, eax; // Define all flags
    mov edi, 1 << 31;
    dec edi;
    RET_FLAGS;
)


TEST_CASE("/instr/add/64", 02222, 0,
    add rdi, rsi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/add/32", 02222, 0,
    add edi, esi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/add/16", 02222, 0,
    add di, si;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/add/8", 02222, 0,
    add dil, sil;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/add/flags", 02222, 0,
    add rdi, rsi;
    RET_FLAGS;
)


TEST_CASE("/instr/sub/64", 02222, 0,
    sub rdi, rsi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/sub/32", 02222, 0,
    sub edi, esi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/sub/16", 02222, 0,
    sub di, si;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/sub/8", 02222, 0,
    sub dil, sil;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/sub/flags", 02222, 0,
    sub rdi, rsi;
    RET_FLAGS;
)
TEST_CASE("/instr/cmp/flags", 02222, 0,
    cmp rdi, rsi;
    RET_FLAGS;
)


TEST_CASE("/instr/mul/64", 02222, 0,
    mov rax, rdi;
    mul rsi;
    xor rax, rdx;
    ret;
)
TEST_CASE("/instr/mul/32", 02222, 0,
    mov rax, rdi;
    mul esi;
    xor rax, rdx;
    ret;
)
TEST_CASE("/instr/mul/16", 02222, 0,
    mov rax, rdi;
    xor edx, edx;
    mul si;
    xor rax, rdx;
    ret;
)
TEST_CASE("/instr/mul/8", 02222, 0,
    mov rax, rdi;
    mul sil;
    ret;
)
TEST_CASE("/instr/mul/flags-64", 02222, 0,
    mov rax, rdi;
    mul rsi;
    RET_FLAGS_MASK(OF|CF);
)
TEST_CASE("/instr/mul/flags-8", 02222, 0,
    mov rax, rdi;
    mul sil;
    RET_FLAGS_MASK(OF|CF);
)

TEST_CASE("/instr/imul1/64", 02222, 0,
    mov rax, rdi;
    imul rsi;
    xor rax, rdx;
    ret;
)
TEST_CASE("/instr/imul1/32", 02222, 0,
    mov rax, rdi;
    imul esi;
    xor rax, rdx;
    ret;
)
TEST_CASE("/instr/imul1/16", 02222, 0,
    mov rax, rdi;
    xor edx, edx;
    imul si;
    xor rax, rdx;
    ret;
)
TEST_CASE("/instr/imul1/8", 02222, 0,
    mov rax, rdi;
    imul sil;
    ret;
)
TEST_CASE("/instr/imul1/flags-64", 02222, 0,
    mov rax, rdi;
    imul rsi;
    RET_FLAGS_MASK(OF|SF|CF);
)
TEST_CASE("/instr/imul1/flags-8", 02222, 0,
    mov rax, rdi;
    imul sil;
    RET_FLAGS_MASK(OF|SF|CF);
)

TEST_CASE("/instr/imul2/64", 02222, 0,
    imul rdi, rsi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/imul2/32", 02222, 0,
    imul edi, esi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/imul2/16", 02222, 0,
    imul di, si;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/imul2/flags", 02222, 0,
    imul rdi, rsi;
    RET_FLAGS_MASK(OF|SF|CF);
)

TEST_CASE("/instr/imul3/64", 02222, 0,
    imul rdi, rsi, 0x10;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/imul3/32", 02222, 0,
    imul edi, esi, 0x10;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/imul3/16", 02222, 0,
    imul di, si, 0x10;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/imul3/flags", 02222, 0,
    imul rdi, rsi, 0x10;
    RET_FLAGS_MASK(OF|SF|CF);
)


TEST_CASE("/instr/and/64", 02222, 0,
    and rdi, rsi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/and/32", 02222, 0,
    and edi, esi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/and/16", 02222, 0,
    and di, si;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/and/8", 02222, 0,
    and dil, sil;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/and/flags", 02222, 0,
    and rdi, rsi;
    RET_FLAGS_MASK(OF|SF|ZF|PF|CF);
)
TEST_CASE("/instr/test/flags", 02222, 0,
    test rdi, rsi;
    RET_FLAGS_MASK(OF|SF|ZF|PF|CF);
)


TEST_CASE("/instr/or/64", 02222, 0,
    or rdi, rsi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/or/32", 02222, 0,
    or edi, esi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/or/16", 02222, 0,
    or di, si;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/or/8", 02222, 0,
    or dil, sil;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/or/flags", 02222, 0,
    or rdi, rsi;
    RET_FLAGS_MASK(OF|SF|ZF|PF|CF);
)


TEST_CASE("/instr/xor/64", 02222, 0,
    xor rdi, rsi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/xor/32", 02222, 0,
    xor edi, esi;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/xor/16", 02222, 0,
    xor di, si;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/xor/8", 02222, 0,
    xor dil, sil;
    mov rax, rdi;
    ret;
)
TEST_CASE("/instr/xor/clear", 020, 0,
    xor eax, eax;
    ret;
)
TEST_CASE("/instr/xor/flags", 02222, 0,
    xor rdi, rsi;
    RET_FLAGS_MASK(OF|SF|ZF|PF|CF);
)



TEST_CASE("/instr/push/64", 0221, 0,
    mov rdx, rsp;
    push rdi;
    mov rax, [rsp];
    sub rdx, rsp;
    xor rax, rdx;
    add rsp, 8;
    ret;
)
TEST_CASE("/instr/pop/64", 0221, 0,
    mov rdx, rsp;
    sub rsp, 8;
    mov [rsp], rdi;
    pop rax;
    sub rdx, rsp;
    xor rax, rdx;
    ret;
)
TEST_CASE("/instr/leave", 0221, 0,
    push rbp;
    mov rax, rdi;
    push rdi;
    mov rbp, rsp;
    xor esp, esp;
    leave;
    sub rax, rbp;
    pop rbp;
    ret;
)
TEST_CASE("/transform/stackframe", 0221, 0,
    push rbp;
    mov rbp, rsp;
    mov [rbp - 8], rdi;
    mov rax, [rbp - 8];
    add rax, 10;
    leave;
    ret;
)
