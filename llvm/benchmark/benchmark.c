
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

typedef void(*StencilFunction)(void*, void*, void*, uint64_t);
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

#define STENCIL_INTERLINES 32


// #define makeDynamic(x) (x)

#define STENCIL_N ((STENCIL_INTERLINES) * 8 + 8)
#define STENCIL_INDEX(x,y) ((y) * ((STENCIL_N) + 1) + (x))
#define STENCIL_OFFSET(base,x,y) ((base) + (y) * ((STENCIL_N) + 1) + (x))


static void
stencil_inner_native_interleaved(void* a, double* restrict b, double* restrict c, uint64_t index)
{
    c[index-1] = 0.25 * (b[STENCIL_OFFSET(index-1, 0, -1)] + b[STENCIL_OFFSET(index-1, 0, 1)] + b[STENCIL_OFFSET(index-1, -1, 0)] + b[STENCIL_OFFSET(index-1, 1, 0)]);
    c[index] = 0.25 * (b[STENCIL_OFFSET(index, 0, -1)] + b[STENCIL_OFFSET(index, 0, 1)] + b[STENCIL_OFFSET(index, -1, 0)] + b[STENCIL_OFFSET(index, 1, 0)]);
}

static void
stencil_inner_native(void* a, double* restrict b, double* restrict c, uint64_t index)
{
    c[index] = 0.25 * (b[STENCIL_OFFSET(index, 0, -1)] + b[STENCIL_OFFSET(index, 0, 1)] + b[STENCIL_OFFSET(index, -1, 0)] + b[STENCIL_OFFSET(index, 1, 0)]);
}

static void
stencil_inner_native2(Stencil* restrict s, double* restrict b, double* restrict c, uint64_t index)
{
    stencil_inner_native(s, b, c, index-1);
    stencil_inner_native(s, b, c, index);
}

static void
stencil_inner_struct_interleaved(Stencil* restrict s, double* restrict b, double* restrict c, uint64_t index)
{
    double result1 = 0;
    double result2 = 0;
    for(uint64_t i = 0; i < s->points; i++)
    {
        StencilPoint* p = s->p + i;
        result2 += p->factor * b[STENCIL_OFFSET(index - 1, p->xdiff, p->ydiff)];
        result1 += p->factor * b[STENCIL_OFFSET(index, p->xdiff, p->ydiff)];
    }
    c[index-1] = result2;
    c[index] = result1;
}

static void
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

static void
stencil_inner_struct2(Stencil* restrict s, double* restrict b, double* restrict c, uint64_t index)
{
    stencil_inner_struct(s, b, c, index-1);
    stencil_inner_struct(s, b, c, index);
}

static inline void
stencil_inner_sorted_struct_interleaved(SortedStencil* restrict s, double* restrict b, double* restrict c, uint64_t index)
{
    double result1 = 0, sum1 = 0;
    double result2 = 0, sum2 = 0;
    for (uint64_t i = 0; i < s->factors; i++)
    {
        StencilFactor* sf = s->f + i;
        StencilPoint* p = sf->p;
        sum2 = b[STENCIL_OFFSET(index-1, p->xdiff, p->ydiff)];
        sum1 = b[STENCIL_OFFSET(index, p->xdiff, p->ydiff)];
        for (uint64_t j = 1; j < sf->points; j++)
        {
            p = sf->p + j;
            sum2 += b[STENCIL_OFFSET(index-1, p->xdiff, p->ydiff)];
            sum1 += b[STENCIL_OFFSET(index, p->xdiff, p->ydiff)];
        }
        result2 += sf->factor * sum2;
        result1 += sf->factor * sum1;
    }
    c[index-1] = result2;
    c[index] = result1;
}

static inline void
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

static void
stencil_inner_sorted_struct2(SortedStencil* restrict s, double* restrict b, double* restrict c, uint64_t index)
{
    stencil_inner_sorted_struct(s, b, c, index-1);
    stencil_inner_sorted_struct(s, b, c, index);
}

static void
compute_jacobi(void* restrict a, StencilFunction fn, double* restrict b, double* restrict c)
{
    uint64_t i, j;
    for (uint64_t iter = 0; iter < 256; iter = iter + 1)
    {
        double* temp = c;
        c = b;
        b = temp;

        for (i = 1; i < STENCIL_N; i = i + 1)
        {
            for (j = 1; j < STENCIL_N; j = j + 1)
                fn(a, b, c, STENCIL_INDEX(j, i));
        }
    }
}

static void
compute_jacobi2(void* restrict a, StencilFunction fn, double* restrict b, double* restrict c)
{
    uint64_t i, j;
    for (uint64_t iter = 0; iter < 256; iter = iter + 1)
    {
        double* temp = c;
        c = b;
        b = temp;

        for (i = 1; i < STENCIL_N; i = i + 1)
        {
            double c0 = c[STENCIL_INDEX(0, i)];
            for (j = 1; j < STENCIL_N; j = j + 1)
                fn(a, b, c, STENCIL_INDEX(j, i));
            c[STENCIL_INDEX(0, i)] = c0;
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
    TestFunction wrapper;
    StencilFunction fn;
    TestParameters params;
};

typedef struct BenchmarkStencilConfig BenchmarkStencilConfig;

static const BenchmarkStencilConfig benchmarkConfigs[] = {
    { compute_jacobi, (StencilFunction) stencil_inner_native, prepare_stencil_native },
    { compute_jacobi2, (StencilFunction) stencil_inner_native2, prepare_stencil_native },
    { compute_jacobi2, (StencilFunction) stencil_inner_native_interleaved, prepare_stencil_native },

    { compute_jacobi, (StencilFunction) stencil_inner_struct, prepare_stencil_struct },
    { compute_jacobi2, (StencilFunction) stencil_inner_struct2, prepare_stencil_struct },
    { compute_jacobi2, (StencilFunction) stencil_inner_struct_interleaved, prepare_stencil_struct },

    { compute_jacobi, (StencilFunction) stencil_inner_sorted_struct, prepare_stencil_sorted_struct },
    { compute_jacobi2, (StencilFunction) stencil_inner_sorted_struct2, prepare_stencil_sorted_struct },
    { compute_jacobi2, (StencilFunction) stencil_inner_sorted_struct_interleaved, prepare_stencil_sorted_struct },
};


JTimer timerTotal, timerCompile, timerRun;

static
Rewriter*
benchmark_init_dbrew(StencilFunction fn)
{
    Rewriter* r = dbrew_new();
    // dbrew_set_ver
    dbrew_verbose(r, false, false, false);
    dbrew_set_decoding_capacity(r, 100000, 100);
    dbrew_set_capture_capacity(r, 100000, 100, 10000);
    dbrew_set_function(r, (uintptr_t) fn);
    dbrew_config_staticpar(r, 0);

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
        .noaliasParams = 7,
        .fixFirstParam = true,
        .firstParam = 0,
        .firstParamLength = 0x100,
    };

    LLState* state = NULL;
    Rewriter* r = NULL;

    TestParameters params = config->params;
    TestFunction testfn = config->wrapper;
    StencilFunction stencilfn = config->fn;

    for (size_t i = 0; i < args->iterationCount; i++)
    {
        params(&arg0, &arg1, &arg2);

        llconfig.firstParam = (uintptr_t) arg0;

        JTimerCont(&timerTotal);
        JTimerCont(&timerCompile);

        StencilFunction processed;

        if (args->mode != BENCHMARK_PLAIN) r = benchmark_init_dbrew(stencilfn);

        if (args->mode == BENCHMARK_LLVM || args->mode == BENCHMARK_DBREW_LLVM_TWICE)
        {
            state = ll_engine_init();
            ll_engine_enable_unsafe_pointer_optimizations(state, true);
        }

        switch (args->mode)
        {
            case BENCHMARK_PLAIN:
                processed = stencilfn;
                break;
            case BENCHMARK_DBREW:
                processed = (StencilFunction) dbrew_rewrite(r, arg0, arg1, arg2, 20);
                break;
            case BENCHMARK_LLVM:
                {
                    LLFunction* llfn = ll_decode_function(r, (uintptr_t) stencilfn, &llconfig, state);
                    assert(!ll_function_build_ir(llfn, state));
                    ll_engine_optimize(state, 3);
                    // ll_engine_dump(state);
                    processed = ll_function_get_pointer(llfn, state);
                }
                break;
            case BENCHMARK_DBREW_LLVM_TWICE:
                {
                    processed = (StencilFunction) dbrew_llvm_rewrite(r, arg0, arg1, arg2, 20);
                    LLFunction* llfn = ll_decode_function(r, (uintptr_t) processed, &llconfig, state);
                    assert(!ll_function_build_ir(llfn, state));
                    ll_engine_optimize(state, 3);
                    // ll_engine_dump(state);
                    processed = ll_function_get_pointer(llfn, state);
                }
                break;
            case BENCHMARK_DBREW_LLVM:
                processed = (StencilFunction) dbrew_llvm_rewrite(r, arg0, arg1, arg2, 20);
                break;
            default:
                assert(0);
        }


        JTimerStop(&timerCompile);

        if (i == 0 && args->decodeGenerated)
        {
            JTimerStop(&timerTotal);
            if (state == NULL) state = ll_engine_init();
            if (r == NULL) r = benchmark_init_dbrew(stencilfn);
            ll_decode_function(r, (uintptr_t) processed, &llconfig, state);
            JTimerCont(&timerTotal);
        }

        JTimerCont(&timerRun);
        for (size_t runs = 0; runs < args->runCount; runs++)
            testfn(arg0, processed, arg1, arg2);

        JTimerStop(&timerRun);
        JTimerStop(&timerTotal);

        if (i == 0) print_matrix(arg2);
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
        printf("Usage: %s [config] [mode] [compiles] [runs per compile]", argv[0]);
        return 1;
    }
    BenchmarkArgs args = {
        .mode = atoi(argv[2]),
        .iterationCount = atoi(argv[3]),
        .runCount = atoi(argv[4]),
        .decodeGenerated = true,
    };

    int configIndex = atoi(argv[1]);
    if (configIndex >= 9 || configIndex < 0)
    {
        return 75;
    }

    const BenchmarkStencilConfig* config = &benchmarkConfigs[configIndex];

    benchmark_run2(&args, config);

    printf("Mode %d Config %d Times %f %f %f\n", args.mode, configIndex, JTimerRead(&timerTotal), JTimerRead(&timerCompile), JTimerRead(&timerRun));
    printf("Normalized %f %f %f\n", JTimerRead(&timerTotal) / args.iterationCount, JTimerRead(&timerCompile) / args.iterationCount, JTimerRead(&timerRun) / args.iterationCount);

    return 0;
}
