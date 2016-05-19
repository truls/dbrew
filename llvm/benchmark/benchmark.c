
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

typedef void(*TestParameters)(void**, void**, void**);
typedef void(*TestFunction)(void*, void*, void*);
typedef void(*StencilFunction)(void*, void*, void*, uint64_t);


#define BENCHMARK_TEST_COUNT 32
static void
benchmark_function_test(uint64_t* restrict a, uint64_t* restrict b)
{
    for (int i = 0; i < BENCHMARK_TEST_COUNT; i++)
    {
        b[i] *= a[i];
    }
}

static void
benchmark_parameters_test(uint64_t** arg0, uint64_t** arg1)
{
    uint64_t* a = malloc(sizeof(uint64_t) * BENCHMARK_TEST_COUNT);
    uint64_t* b = malloc(sizeof(uint64_t) * BENCHMARK_TEST_COUNT);
    for (int i = 0; i < BENCHMARK_TEST_COUNT; i++)
    {
        a[i] = 3;
        b[i] = i * i + 5;
    }

    *arg0 = a;
    *arg1 = b;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

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

#define BENCHMARK_STENCIL_INTERLINES 32
// #define makeDynamic(x) (x)

#define STENCIL_N ((BENCHMARK_STENCIL_INTERLINES) * 8 + 8)
#define STENCIL_INDEX(x,y) ((y) * ((STENCIL_N) + 1) + (x))
#define STENCIL_OFFSET(base,x,y) ((base) + (y) * ((STENCIL_N) + 1) + (x))

static void
stencil_inner_native(Stencil* restrict a, double* restrict b, double* restrict c, uint64_t index)
{
    uint64_t N = BENCHMARK_STENCIL_INTERLINES * 8 + 8;
    c[index] = 0.25 * (b[index - 1] + b[index + 1] + b[index - (N+1)] + b[index + (N+1)]);
}

static void
stencil_inner_struct(Stencil* restrict s, double* restrict b, double* restrict c, uint64_t index)
{
    uint64_t N = BENCHMARK_STENCIL_INTERLINES * 8 + 8;
    double result1 = 0;
    double result2 = 0;
    for(uint64_t i = 0; i < s->points; i++)
    {
        StencilPoint* p = s->p + i;
        result1 += p->factor * b[index + p->xdiff + p->ydiff * (N+1)];
        result2 += p->factor * b[index+1 + p->xdiff + p->ydiff * (N+1)];
    }
    c[index] = result1;
    // c[index+1] = result2;
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
benchmark_function_stencil(Stencil* restrict a, StencilFunction fn, double* restrict b, double* restrict c)
{
    uint64_t i, j;
    for (uint64_t iter = 0; iter < 4096; iter = makeDynamic(iter) + 1)
    {
        double* temp = c;
        c = b;
        b = temp;

        for (i = 1; i < STENCIL_N; i = makeDynamic(i) + 1)
        {
            for (j = 1; j < STENCIL_N; j = makeDynamic(j) + 2)
                fn(a, b, c, STENCIL_INDEX(j, i));

            c[STENCIL_INDEX(0, i)] = 1.0 - (i * 1.0 / STENCIL_N);
        }
    }
}

static void
benchmark_parameters_stencil(void** arg0, void** arg1, void** arg2)
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
    *arg2 = malloc(sizeof(double) * (STENCIL_N + 1) * (STENCIL_N + 1));
    memcpy(*arg2, b, sizeof(double) * (STENCIL_N + 1) * (STENCIL_N + 1));

    *arg0 = &s5s;
    *arg1 = b;
}

static void
print_matrix(double* b)
{
    printf("Matrix:\n");

    for (int y = 0; y < 9; y++)
    {
        for (int x = 0; x < 9; x++)
        {
            int index = STENCIL_INDEX(x * (BENCHMARK_STENCIL_INTERLINES + 1), y * (BENCHMARK_STENCIL_INTERLINES + 1));
            printf("%7.4f", b[index]);
        }
        printf("\n");
    }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////




enum BenchmarkMode {
    BENCHMARK_PLAIN,
    BENCHMARK_DBREW,
    BENCHMARK_LLVM,
    BENCHMARK_DBREW_LLVM,
    BENCHMARK_DBREW_LLVM_TWICE,
};

typedef enum BenchmarkMode BenchmarkMode;

JTimer timerTotal, timerCompile, timerRun;

static
void
benchmark_run(BenchmarkMode mode, int count, StencilFunction fn, void* arg0, void* arg1, void* arg2)
{
    JTimerCont(&timerTotal);
    LLConfig config = {
        .name = "test",
        .stackSize = 128,
        .noaliasParams = 7,
        .fixFirstParam = false,
        .firstParam = (uintptr_t) arg0,
        .firstParamLength = 0,
    };

    JTimerCont(&timerCompile);

    LLState* state = ll_engine_init();
    Rewriter* r = dbrew_new();
    // dbrew_set_ver
    dbrew_verbose(r, true, true, true);
    dbrew_set_decoding_capacity(r, 100000, 100);
    dbrew_set_capture_capacity(r, 100000, 100, 10000);
    dbrew_set_function(r, (uintptr_t) fn);
    dbrew_config_staticpar(r, 0);

    StencilFunction processed;

    switch (mode)
    {
        case BENCHMARK_PLAIN:
            processed = fn;
            break;
        case BENCHMARK_DBREW:
            processed = (StencilFunction) dbrew_rewrite(r, arg0, arg1, arg2, 20);
            break;
        case BENCHMARK_LLVM:
            {
                LLFunction* llfn = ll_decode_function(r, (uintptr_t) fn, &config, state);
                assert(!ll_function_build_ir(llfn, state));
                ll_engine_optimize(state, 3);
                // ll_engine_dump(state);
                processed = ll_function_get_pointer(llfn, state);
            }
            break;
        case BENCHMARK_DBREW_LLVM_TWICE:
        case BENCHMARK_DBREW_LLVM:
            processed = (StencilFunction) dbrew_llvm_rewrite(r, arg0, arg1, arg2, 20);
            break;
        default:
            assert(0);
    }

    if (mode == BENCHMARK_DBREW_LLVM_TWICE) {

        LLFunction* llfn = ll_decode_function(r, (uintptr_t) processed, &config, state);
        assert(!ll_function_build_ir(llfn, state));
        ll_engine_optimize(state, 3);
        // ll_engine_dump(state);
        processed = ll_function_get_pointer(llfn, state);
    }

    ll_decode_function(r, (uintptr_t) processed, &config, state);

    JTimerStop(&timerCompile);
    JTimerCont(&timerRun);
    for (int i = 0; i < count; i++)
        benchmark_function_stencil(arg0, processed, arg1, arg2);
    //     processed(arg0, arg1, arg2);
    JTimerStop(&timerRun);
    JTimerStop(&timerTotal);
}

int
main(int argc, char** argv)
{
    BenchmarkMode mode = BENCHMARK_PLAIN;
    int count = 1;
    if (argc >= 2) count = atoi(argv[1]);
    if (argc >= 3) mode = atoi(argv[2]);

    void* arg0;
    void* arg1;
    void* arg2;

    StencilFunction fn = (StencilFunction) stencil_inner_sorted_struct2;
    TestParameters params = (TestParameters) benchmark_parameters_stencil;

    params(&arg0, &arg1, &arg2);

    JTimerInit(&timerTotal);
    JTimerInit(&timerCompile);
    JTimerInit(&timerRun);

    print_matrix(arg1);
    benchmark_run(mode, count, fn, arg0, arg1, arg2);
    print_matrix(arg2);

    printf("Mode %d Times %f %f %f\n", mode, JTimerRead(&timerTotal), JTimerRead(&timerCompile), JTimerRead(&timerRun));

    return 0;
}
