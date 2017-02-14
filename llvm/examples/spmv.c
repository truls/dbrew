
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <dbrew.h>
#include <dbrew-llvm.h>
#include <../benchmark/timer.h>

// Include generated test matrix, provides rawMatrix and rawVector.
#include "spmv-matrix.gen"

struct Entry {
    size_t a;
    size_t b;
};

typedef struct Entry Entry;

struct SpMatrix {
    size_t width;
    size_t height;
    // First HEIGHT entries consists of start/end index in entries array.
    // Others are in order (!) tuples of column/value.
    // Lookup therefore can be done in O(width).
    Entry entries[0];
};

typedef struct SpMatrix SpMatrix;


static
void
spmv_c(SpMatrix* matrix, double* restrict v, double* restrict r)
{
    for (size_t j = 0; j < matrix->height; j++)
    {
        double sum = 0;
        Entry* rowEntry = &matrix->entries[j];
        for (size_t i = rowEntry->a; i < rowEntry->b; i++)
        {
            Entry* entry = &matrix->entries[i];
            double* d = (double*) &entry->b;
            sum += v[entry->a] * *d;
        }
        r[j] = sum;
    }
}

#define ASM_BLOCK(...) __asm__(".intel_syntax noprefix; " #__VA_ARGS__ "; .att_syntax")

void spmv_asm(SpMatrix*, double*, double*);
ASM_BLOCK(
.globl spmv_asm;
.align 0x10;
spmv_asm:
    mov r9, [rdi + 8]; // r9 = rowcount
    xor r8, r8; // r8 = current row
    lea rax, [r9 * 2];
    test r9, r9;
    lea rcx, [rdi + 16]; // rcx = ptr to current row entry
    lea rdi, [rdi + rax * 8 + 16]; // rdi = ptr to current value entry
    jz 2f;
.align 0x10;
1:
    mov rax, [rcx + 8];
    xorpd xmm0, xmm0;
    sub rax, [rcx]; // rax = number of value entries for current row
    jz 3f;
.align 0x10;
4:
    mov r10, [rdi];
    add rdi, 16;
    movsd xmm2, [rsi + 8 * r10];
    mulsd xmm2, [rdi - 8];
    sub rax, 1;
    addsd xmm0, xmm2;
    jnz 4b;
3:
    movsd [rdx + 8 * r8], xmm0;
    add rcx, 16;
    add r8, 1;
    cmp r8, r9;
    jne 1b;
2:
    ret;
);

int
main(void)
{
    JTimer timer;

    Rewriter* r = dbrew_new();
    dbrew_set_capture_capacity(r, 1000000, 1, 1000000);
    dbrew_set_decoding_capacity(r, 10000, 100);
    dbrew_set_function(r, (uint64_t) spmv_asm);
    dbrew_config_parcount(r, 3);
    dbrew_config_staticpar(r, 0);
    dbrew_optverbose(r, false);
    dbrew_verbose(r, false, false, false);

    LLConfig config = {
        .name = "spmv",
        .stackSize = 0,
        .signature = 011113 // void (i8* noalias, i8* noalias, i8* noalias)
    };

    LLState* state = ll_engine_init();
    ll_engine_enable_fast_math(state, true);
    ll_engine_enable_full_loop_unroll(state, true);
    LLFunction* fn = ll_decode_function((uintptr_t) spmv_asm, (DecodeFunc) dbrew_decode, r, &config, state);
    LLFunction* fnspec = ll_function_specialize(fn, 0, (uintptr_t) rawMatrix, sizeof(rawMatrix), state);

    ll_engine_optimize(state, 3);
    // ll_engine_dump(state);
    // ll_engine_disassemble(state);

    SpMatrix* mat = (SpMatrix*) rawMatrix;
    double* v = (double*) rawVector;
    double* rv1 = calloc(sizeof(double), mat->height);
    double* rv2 = calloc(sizeof(double), mat->height);
    double* rv3 = calloc(sizeof(double), mat->height);

    uint8_t* (*spmv_spec)(SpMatrix*, double*, double*) = ll_function_get_pointer(fnspec, state);
    uint8_t* (*spmv_dbrew)(SpMatrix*, double*, double*) = (void*) dbrew_rewrite(r, mat, v, rv1);
    uint8_t* (*spmv_dbrew_llvm)(SpMatrix*, double*, double*) = (void*) dbrew_llvm_rewrite(r, mat, v, rv1);

    // Original
    JTimerInit(&timer);
    JTimerCont(&timer);
    for (int i = 0; i < 10000000; i++) spmv_c(mat, v, rv1);
    JTimerStop(&timer);
    printf("Time: %lf secs\n", JTimerRead(&timer));
    for (size_t i = 0; i < mat->height; i++) printf(" %lf", rv1[i]);
    printf("\n");

    // ASM
    JTimerInit(&timer);
    JTimerCont(&timer);
    for (int i = 0; i < 10000000; i++) spmv_asm(mat, v, rv1);
    JTimerStop(&timer);
    printf("Time: %lf secs\n", JTimerRead(&timer));
    for (size_t i = 0; i < mat->height; i++) printf(" %lf", rv1[i]);
    printf("\n");

    // LLVM fixed
    // Running time can be reduced by 38% when using a small code model, which
    // leads to RIP-relative addressing and therefore much smaller code.
    // Add "options.CodeModel = LLVMCodeModelSmall;" to llengine.c. Note that
    // any memory access via non-parametric addresses will cause a segfault.
    JTimerInit(&timer);
    JTimerCont(&timer);
    for (int i = 0; i < 10000000; i++) spmv_spec(mat, v, rv2);
    JTimerStop(&timer);
    printf("Time: %lf secs\n", JTimerRead(&timer));
    for (size_t i = 0; i < mat->height; i++) printf(" %lf", rv2[i]);
    printf("\n");

    // DBrew
    JTimerInit(&timer);
    JTimerCont(&timer);
    for (int i = 0; i < 10000000; i++) spmv_dbrew(mat, v, rv3);
    JTimerStop(&timer);
    printf("Time: %lf secs\n", JTimerRead(&timer));
    for (size_t i = 0; i < mat->height; i++) printf(" %lf", rv3[i]);
    printf("\n");

    // DBrew+LLVM
    JTimerInit(&timer);
    JTimerCont(&timer);
    for (int i = 0; i < 10000000; i++) spmv_dbrew_llvm(mat, v, rv3);
    JTimerStop(&timer);
    printf("Time: %lf secs\n", JTimerRead(&timer));
    for (size_t i = 0; i < mat->height; i++) printf(" %lf", rv3[i]);
    printf("\n");

    return 0;
}
