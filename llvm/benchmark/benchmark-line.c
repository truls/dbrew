
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <dbrew.h>
#include <llcommon.h>
#include <lldecoder.h>
#include <llengine.h>
#include <llfunction.h>

#include "timer.h"

typedef void(*StencilFunction)(void*, double* restrict, double* restrict, uint64_t);
typedef void(*StencilLineFunction)(void*, double* restrict, double* restrict, uint64_t, StencilFunction);
typedef void(*TestParameters)(void**, void**, void**);
typedef void(*TestFunction)(void*, StencilFunction, double*, double*);


typedef struct {
    uint64_t xdiff, ydiff;
    double factor;
} StencilPoint;

typedef struct {
    uint64_t points;
    StencilPoint p[];
} Stencil;

typedef struct {
    double factor;
    uint64_t points;
    StencilPoint* p;
} StencilFactor;

typedef struct {
    uint64_t factors;
    StencilFactor f[];
} SortedStencil;

Stencil s5 = {4, {{-1,0,0.25},{1,0,0.25},{0,-1,0.25},{0,1,0.25}}};

SortedStencil s5s = {1, {{0.25,4,&(s5.p[0])}}};

#ifndef STENCIL_INTERLINES
#define STENCIL_INTERLINES 0
#endif

#define STENCIL_N ((STENCIL_INTERLINES) * 8 + 8)
#define STENCIL_INDEX(x,y) ((y) * ((STENCIL_N) + 1) + (x))
#define STENCIL_OFFSET(base,x,y) ((base) + (y) * ((STENCIL_N) + 1) + (x))



static
inline
void
stencil_inner_native(void* __attribute__((unused)) a, double* restrict b, double* restrict c, uint64_t index)
{
    c[index] = 0.25 * (b[STENCIL_OFFSET(index, 0, -1)] + b[STENCIL_OFFSET(index, 0, 1)] + b[STENCIL_OFFSET(index, -1, 0)] + b[STENCIL_OFFSET(index, 1, 0)]);
}

static
inline
void
stencil_inner_struct(Stencil* restrict s, double* restrict b, double* restrict c, uint64_t index)
{
    double result1 = 0;
    for(uint64_t i = 0; i < s->points; i++)
    {
        StencilPoint* p = s->p + i;
        result1 += p->factor * b[STENCIL_OFFSET(index, p->xdiff, p->ydiff)];
    }
    c[index] = result1;
}

static
inline
void
stencil_inner_sorted_struct(SortedStencil* restrict s, double* restrict b, double* restrict c, uint64_t index)
{
    double result1 = 0, sum1 = 0;
    for (uint64_t i = 0; i < s->factors; i++)
    {
        StencilFactor* sf = s->f + i;
        StencilPoint* p = sf->p;
        sum1 = b[STENCIL_OFFSET(index, p->xdiff, p->ydiff)];
        for (uint64_t j = 1; j < sf->points; j++)
        {
            p = sf->p + j;
            sum1 += b[STENCIL_OFFSET(index, p->xdiff, p->ydiff)];
        }
        result1 += sf->factor * sum1;
    }
    c[index] = result1;
}

static
void
stencil_line_native(void* a, double* restrict b, double* restrict c, uint64_t index, StencilFunction __attribute__((unused)) fn)
{
    uint64_t j;
    for (j = 1; j < STENCIL_N; ++j)
    {
        stencil_inner_native(a, b, c, STENCIL_OFFSET(index, j, 0));
    }
}

static
void
stencil_line_struct(void* a, double* restrict b, double* restrict c, uint64_t index, StencilFunction __attribute__((unused)) fn)
{
    uint64_t j;
    for (j = 1; j < STENCIL_N; ++j)
    {
        stencil_inner_struct(a, b, c, STENCIL_OFFSET(index, j, 0));
    }
}

static
void
stencil_line_sorted_struct(void* a, double* restrict b, double* restrict c, uint64_t index, StencilFunction __attribute__((unused)) fn)
{
    uint64_t j;
    for (j = 1; j < STENCIL_N; ++j)
    {
        stencil_inner_sorted_struct(a, b, c, STENCIL_OFFSET(index, j, 0));
    }
}



static
void
stencil_line_dbrew(void* a, double* restrict b, double* restrict c, uint64_t index, StencilFunction fn)
{
    uint64_t j;
    for (j = 1; j < STENCIL_N; ++j)
    {
        fn(a, b, c, STENCIL_OFFSET(index, j, 0));
    }
}

static void
compute_jacobi_line(void* restrict a, StencilLineFunction fn, double* restrict b, double* restrict c)
{
    uint64_t i;
    for (uint64_t iter = 0; iter < 1000; iter = iter + 1)
    {
        double* temp = c;
        c = b;
        b = temp;

        for (i = 1; i < STENCIL_N; i = i + 1)
        {
            // printf("%p\n", b);
            fn(a, b, c, STENCIL_INDEX(0, i), NULL);
        }
    }
}

static
void
init_matrix(void** matrixIn, void** matrixOut)
{
    double* b = malloc(sizeof(double) * (STENCIL_N + 1) * (STENCIL_N + 1));
    for (int i = 0; i <= STENCIL_N; i++) {
        for (int j = 0; j <= STENCIL_N; j++) {
            int index = STENCIL_INDEX(j, i);
            if (i == 0) // First Row
                b[index] = 1.0 - (j * 1.0 / STENCIL_N);
            else if (i == STENCIL_N) // Last Row
                b[index] = j * 1.0 / STENCIL_N;
            else if (j == 0) // First Column
                b[index] = 1.0 - (i * 1.0 / STENCIL_N);
            else if (j == STENCIL_N) // Last Column
                b[index] = i * 1.0 / STENCIL_N;
            else
                b[index] = 0;
        }
    }
    *matrixOut = malloc(sizeof(double) * (STENCIL_N + 1) * (STENCIL_N + 1));
    memcpy(*matrixOut, b, sizeof(double) * (STENCIL_N + 1) * (STENCIL_N + 1));

    *matrixIn = b;
}

static void
prepare_stencil_native(void** arg0, void** arg1, void** arg2)
{
    init_matrix(arg1, arg2);

    *arg0 = NULL;
}

static void
prepare_stencil_struct(void** arg0, void** arg1, void** arg2)
{
    init_matrix(arg1, arg2);

    *arg0 = &s5;
}

static void
prepare_stencil_sorted_struct(void** arg0, void** arg1, void** arg2)
{
    init_matrix(arg1, arg2);

    *arg0 = &s5s;
}

static void
print_matrix(double* b)
{
    printf("Matrix:\n");

    for (int y = 0; y < 9; y++)
    {
        for (int x = 0; x < 9; x++)
        {
            int index = STENCIL_INDEX(x * (STENCIL_INTERLINES + 1), y * (STENCIL_INTERLINES + 1));
            printf("%7.4f", b[index]);
        }
        printf("\n");
    }
}



enum BenchmarkMode {
    BENCHMARK_PLAIN,
    BENCHMARK_DBREW,
    BENCHMARK_LLVM,
    BENCHMARK_LLVM_FIXED,
    BENCHMARK_DBREW_LLVM,
    BENCHMARK_DBREW_LLVM_TWICE,
};

typedef enum BenchmarkMode BenchmarkMode;

struct BenchmarkArgs {
    size_t iterationCount;
    size_t runCount;
    BenchmarkMode mode;
    bool decodeGenerated;
};

typedef struct BenchmarkArgs BenchmarkArgs;

struct BenchmarkStencilConfig {
    StencilFunction kernelfn;
    StencilLineFunction linefn;
    TestParameters params;
};

typedef struct BenchmarkStencilConfig BenchmarkStencilConfig;

static const BenchmarkStencilConfig benchmarkConfigs[] = {
    { (StencilFunction) stencil_inner_native, stencil_line_native, prepare_stencil_native },
    { (StencilFunction) stencil_inner_struct, stencil_line_struct, prepare_stencil_struct },
    { (StencilFunction) stencil_inner_sorted_struct, stencil_line_sorted_struct, prepare_stencil_sorted_struct },
};


JTimer timerTotal, timerCompile, timerRun;

static
Rewriter*
benchmark_init_dbrew(void)
{
    Rewriter* r = dbrew_new();
    dbrew_verbose(r, false, false, false);
    dbrew_optverbose(r, false);
    dbrew_set_decoding_capacity(r, 100000, 100);
    dbrew_set_capture_capacity(r, 100000, 100, 10000);
    dbrew_set_function(r, (uintptr_t) stencil_line_dbrew);
    dbrew_config_staticpar(r, 0);
    dbrew_config_staticpar(r, 4);
    dbrew_config_parcount(r, 5);
    dbrew_config_force_unknown(r, 0);

    return r;
}

static
void
benchmark_run2(const BenchmarkArgs* args, const BenchmarkStencilConfig* config)
{
    void* arg0;
    void* arg1;
    void* arg2;

    LLConfig llconfig = {
        .name = "test",
        .stackSize = 128,
        .signature = 0211114,
    };

    LLState* state = NULL;
    Rewriter* r = NULL;

    TestParameters params = config->params;

    for (size_t i = 0; i < args->iterationCount; i++)
    {
        params(&arg0, &arg1, &arg2);

        JTimerCont(&timerTotal);
        JTimerCont(&timerCompile);

        StencilLineFunction processed;

        if (args->mode != BENCHMARK_PLAIN) r = benchmark_init_dbrew();

        if (args->mode == BENCHMARK_LLVM || args->mode == BENCHMARK_LLVM_FIXED || args->mode == BENCHMARK_DBREW_LLVM_TWICE)
        {
            state = ll_engine_init();
            ll_engine_enable_unsafe_pointer_optimizations(state, true);
        }

        switch (args->mode)
        {
            case BENCHMARK_PLAIN:
                processed = config->linefn;
                break;
            case BENCHMARK_DBREW:
                processed = (StencilLineFunction) dbrew_rewrite(r, arg0, arg1, arg2, 20, config->kernelfn);
                break;
            case BENCHMARK_LLVM:
                {
                    LLFunction* llfn = ll_decode_function(r, (uintptr_t) config->linefn, &llconfig, state);
                    bool error = !ll_function_build_ir(llfn, state);
                    assert(!error);
                    ll_engine_optimize(state, 3);
                    if (i == 0 && args->decodeGenerated)
                        ll_engine_dump(state);
                    processed = ll_function_get_pointer(llfn, state);
                }
                break;
            case BENCHMARK_LLVM_FIXED:
                {
                    LLFunction* llfn = ll_decode_function(r, (uintptr_t) config->linefn, &llconfig, state);
                    bool error = !ll_function_build_ir(llfn, state);
                    assert(!error);
                    if (arg0 != NULL)
                        llfn = ll_function_specialize(llfn, 0, (uintptr_t) arg0, 0x100, state);
                    ll_engine_optimize(state, 3);
                    if (i == 0 && args->decodeGenerated)
                        ll_engine_dump(state);
                    processed = ll_function_get_pointer(llfn, state);
                }
                break;
            case BENCHMARK_DBREW_LLVM_TWICE:
                {
                    dbrew_optverbose(r, i == 0 && args->decodeGenerated);
                    processed = (StencilLineFunction) dbrew_llvm_rewrite(r, arg0, arg1, arg2, 20, config->kernelfn);
                    LLFunction* llfn = ll_decode_function(r, (uintptr_t) processed, &llconfig, state);
                    bool error = !ll_function_build_ir(llfn, state);
                    assert(!error);
                    ll_engine_optimize(state, 3);
                    if (i == 0 && args->decodeGenerated)
                        ll_engine_dump(state);
                    processed = ll_function_get_pointer(llfn, state);
                }
                break;
            case BENCHMARK_DBREW_LLVM:
                dbrew_optverbose(r, i == 0 && args->decodeGenerated);
                processed = (StencilLineFunction) dbrew_llvm_rewrite(r, arg0, arg1, arg2, 20, config->kernelfn);
                break;
            default:
                assert(0);
        }


        JTimerStop(&timerCompile);

        if (i == 0 && args->decodeGenerated)
        {
            JTimerStop(&timerTotal);
            if (state == NULL) state = ll_engine_init();
            if (r == NULL) r = benchmark_init_dbrew();
            dbrew_verbose(r, true, false, false);
            ll_decode_function(r, (uintptr_t) processed, &llconfig, state);
            JTimerCont(&timerTotal);
        }

        JTimerCont(&timerRun);
        for (size_t runs = 0; runs < args->runCount; runs++)
            compute_jacobi_line(arg0, processed, arg1, arg2);

        JTimerStop(&timerRun);
        JTimerStop(&timerTotal);

        // Smoke test to see whether results seem to be correct.
        if (i == 0)
            printf("matrix(n-1,n-1) = %f\n", ((double*)arg2)[STENCIL_INDEX(STENCIL_N-1, STENCIL_N-1)]);

        if (state != NULL) {
            ll_engine_dispose(state);
            state = NULL;
        }
        free(arg1);
        free(arg2);
        if (r != NULL)
        {
            dbrew_free(r);
            r = NULL;
        }
    }
}

int
main(int argc, char** argv)
{
    if (argc < 5) {
        printf("Usage: %s [config] [mode] [compiles] [runs per compile] ([decode generated])\n", argv[0]);
        return 1;
    }
    bool decodeGenerated = false;
    if (argc >= 6)
        decodeGenerated = atoi(argv[5]) != 0;

    BenchmarkArgs args = {
        .mode = atoi(argv[2]),
        .iterationCount = atoi(argv[3]),
        .runCount = atoi(argv[4]),
        .decodeGenerated = decodeGenerated,
    };

    int configIndex = atoi(argv[1]);
    if (configIndex >= 3 || configIndex < 0)
    {
        return 75;
    }

    const BenchmarkStencilConfig* config = &benchmarkConfigs[configIndex];

    benchmark_run2(&args, config);

    printf("Mode %d Config %d Times %f %f %f\n", args.mode, configIndex, JTimerRead(&timerTotal), JTimerRead(&timerCompile), JTimerRead(&timerRun));
    printf("Normalized %f %f %f\n", JTimerRead(&timerTotal) / args.iterationCount, JTimerRead(&timerCompile) / args.iterationCount, JTimerRead(&timerRun) / args.iterationCount);

    return 0;
}
