#undef  TEST_NAMESPACE
#define TEST_NAMESPACE condition

#define TEST_CASE_CMOV(instr) TEST_CASE("/condition/" # instr, 02222, 0, \
    xor eax, eax; \
    mov ecx, 1; \
    cmp rdi, rsi; \
    instr eax, ecx; \
    ret; \
)

TEST_CASE_CMOV(cmovo)
TEST_CASE_CMOV(cmovno)
TEST_CASE_CMOV(cmovc)
TEST_CASE_CMOV(cmovnc)
TEST_CASE_CMOV(cmovz)
TEST_CASE_CMOV(cmovnz)
TEST_CASE_CMOV(cmovbe)
TEST_CASE_CMOV(cmova)
TEST_CASE_CMOV(cmovs)
TEST_CASE_CMOV(cmovns)
TEST_CASE_CMOV(cmovp)
TEST_CASE_CMOV(cmovnp)
TEST_CASE_CMOV(cmovl)
TEST_CASE_CMOV(cmovge)
TEST_CASE_CMOV(cmovle)
TEST_CASE_CMOV(cmovg)
