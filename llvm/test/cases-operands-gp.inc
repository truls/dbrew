#undef  TEST_NAMESPACE
#define TEST_NAMESPACE operands_gp

TEST_CASE("/operands/imm/64-64", 020, 0,
    movabs rax, 0x123443215678dcba;
    ret;
)
TEST_CASE("/operands/imm/64-32", 020, 0,
    mov rax, 0xffffffff9678dcba;
    ret;
)
TEST_CASE("/operands/imm/32", 020, 0,
    mov eax, 0xacd3002f;
    ret;
)
TEST_CASE("/operands/imm/16", 020, 0,
    xor eax, eax;
    mov ax, 0xacd3;
    ret;
)
TEST_CASE("/operands/imm/8", 020, 0,
    xor eax, eax;
    mov ah, 0xac;
    mov al, 0x41;
    ret;
)

TEST_CASE("/operands/reg/64", 0221, 0,
    mov rax, rdi;
    ret;
)
TEST_CASE("/operands/reg/32", 02222, 0,
    mov rax, rsi;
    mov eax, edi;
    ret;
)
TEST_CASE("/operands/reg/16", 02222, 0,
    mov rax, rsi;
    mov ax, di;
    ret;
)
TEST_CASE("/operands/reg/8", 02222, 0,
    mov eax, esi;
    mov ax, di;
    mov ah, al;
    add ah, 1;
    mov al, ah;
    ret;
)

TEST_CASE("/operands/mem/64", 0221, 0,
    sub rsp, 8;
    mov [rsp], rdi;
    add qword ptr [rsp], 1;
    mov rax, [rsp];
    add rsp, 8;
    ret;
)
TEST_CASE("/operands/mem/32", 0221, 0,
    sub rsp, 8;
    mov [rsp], edi;
    add dword ptr [rsp], 1;
    mov eax, [rsp];
    add rsp, 8;
    ret;
)
TEST_CASE("/operands/mem/16", 0221, 0,
    sub rsp, 8;
    xor eax, eax;
    mov [rsp], di;
    add word ptr [rsp], 1;
    mov ax, [rsp];
    add rsp, 8;
    ret;
)
TEST_CASE("/operands/mem/8", 0221, 0,
    sub rsp, 8;
    xor eax, eax;
    mov [rsp], dil;
    add byte ptr [rsp], 1;
    mov al, [rsp];
    add rsp, 8;
    ret;
)

#define TEST_CASE_AGEN(subname,...) \
        TEST_CASE_NAMED("/operands/agen/int64/" subname, int64, 02222, 0, lea rax, __VA_ARGS__ ; ret) \
        TEST_CASE_NAMED("/operands/agen/int32/" subname, int32, 02222, 0, lea eax, __VA_ARGS__ ; ret) \
        TEST_CASE_NAMED("/operands/agen/int16/" subname, int16, 02222, 0, xor eax, eax; lea ax, __VA_ARGS__ ; ret) \
        TEST_CASE_NAMED("/operands/agen/ptr/" subname, ptr, 02002, 0, lea rax, __VA_ARGS__ ; ret)

TEST_CASE_AGEN("imm", [0x12345678])
TEST_CASE_AGEN("rip", [rip + 1f] ; 1:)
TEST_CASE_AGEN("reg", [rdi])
TEST_CASE_AGEN("regoff", [rdi + 0x12345678])
TEST_CASE_AGEN("ireg/1", [1 * rsi])
TEST_CASE_AGEN("ireg/2", [2 * rsi])
TEST_CASE_AGEN("ireg/4", [4 * rsi])
TEST_CASE_AGEN("ireg/8", [8 * rsi])
TEST_CASE_AGEN("sreg/1", [rdi + 1 * rsi])
TEST_CASE_AGEN("sreg/2", [rdi + 2 * rsi])
TEST_CASE_AGEN("sreg/4", [rdi + 4 * rsi])
TEST_CASE_AGEN("sreg/8", [rdi + 8 * rsi])
TEST_CASE_AGEN("sregoff/1", [rdi + 1 * rsi + 0x12345678])
TEST_CASE_AGEN("sregoff/2", [rdi + 2 * rsi + 0x12345678])
TEST_CASE_AGEN("sregoff/4", [rdi + 4 * rsi + 0x12345678])
TEST_CASE_AGEN("sregoff/8", [rdi + 8 * rsi + 0x12345678])

TEST_CASE("/operands/reg/ptr", 0221, 0,
    push rdi;
    mov rdi, rsp; // A pointer
    mov rax, [rdi];
    add rsp, 8;
    ret;
)
