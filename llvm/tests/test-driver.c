//!compile = {cc} {ccflags} {ldflags} -I. -I../../include -I../../include/priv -I../include -std=c99 -g -o {outfile} {infile} {driver} ../../libdbrew.a ../libdbrew-llvm.a {ldlibs}
//!cc = cc

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <dbrew.h>
#include <llcommon.h>
#include <lldecoder.h>
#include <llengine.h>
#include <llfunction.h>

#define _CSRC
#include "test-common.h"

typedef long(*TestFunction)();
typedef void(*TestRoutine)(TestFunction);

// The test case against this code is linked contains the necessary information.
extern TestCase testCase;


static void
runTestSingleInt(TestFunction fn)
{
    long q = fn(10);
    printf("Result: %ld\n", q);
}

static void
runTestInt(TestFunction fn)
{
    signed long testData[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    long q = fn(testData);
    printf("Result: %ld %ld %ld %ld %ld %ld %ld %ld %ld\n", q, testData[0], testData[1], testData[2], testData[3], testData[4], testData[5], testData[6], testData[7]);
}

static void
runTestDouble(TestFunction fn)
{
    double testData1[4] = { 1.0, 2.0, 3.0, 3.0 };
    double testData2[4] = { 2.0, 1.0, 3.0, 3.0 };
    long q = fn(testData1, testData2);
    printf("Result: %ld %f %f %f %f\n", q, testData1[0], testData1[1], testData1[2], testData1[3]);
}

static void
runTestFloatArray(TestFunction fn)
{
    float testData1[4] = { 1.0, 2.0, 3.0, 3.0 };
    float testData2[4] = { 2.0, 1.0, 3.0, 3.0 };
    long q = fn(testData1, testData2);
    printf("Result: %ld %f %f %f %f\n", q, testData1[0], testData1[1], testData1[2], testData1[3]);
}

static void
runTestStencilInt(TestFunction fn)
{
    void* stencil = ((void**) testCase.data)[0];
    long matrixData[9] = { 0, 2, 0, 2, 1, 2, 0, 2, 0 };
    long* matrix[3] = { matrixData, matrixData + 3, matrixData + 6 };
    long result = fn(stencil, (long**) matrix, 1, 1);
    printf("Result: %lx\n", result);
}

static void
runTestStencilDouble(TestFunction fn)
{
    void* stencil = ((void**) testCase.data)[0];
    double matrixData[9] = { 0, 3.5, 1.2, 1.5, 1, 2.1, 0.8, 4.9, 0 };
    double* matrix[3] = { matrixData, matrixData + 3, matrixData + 6 };
    long result = fn(stencil, (double**) matrix, 1, 1);
    double* resultPtr = (double*) &result;
    printf("Result: %f\n", *resultPtr);
}

static TestRoutine testRoutines[] = {
    [TEST_DRIVER_INT_ARRAY] = runTestInt,
    [TEST_DRIVER_DOUBLE_ARRAY] = runTestDouble,
    [TEST_DRIVER_INT] = runTestSingleInt,
    [TEST_DRIVER_STENCIL_INT] = runTestStencilInt,
    [TEST_DRIVER_STENCIL_DOUBLE] = runTestStencilDouble,
    [TEST_DRIVER_FLOAT_ARRAY] = runTestFloatArray,
};

static
int
test_dbrew_binding(bool debug)
{
    Rewriter* dbrew = dbrew_new();

    if (testCase.routineIndex != -1)
        return 1;

    dbrew_verbose(dbrew, false, false, false);
    dbrew_set_decoding_capacity(dbrew, 100000, 100);
    dbrew_set_capture_capacity(dbrew, 100000, 100, 10000);
    dbrew_set_function(dbrew, (uintptr_t) testCase.function);
    // dbrew_config_staticpar(dbrew, 0);


    uintptr_t fn = dbrew_llvm_rewrite(dbrew, 10);

    if (debug)
    {
        LLConfig config = {
            .name = "test",
            .stackSize = 128,
            .noaliasParams = 7,
            .fixFirstParam = false
        };

        LLState* state = ll_engine_init();
        ll_decode_function(dbrew, fn, &config, state);
    }

    return 0;
}

static
int
test_llvm_generation(bool debug)
{
    Rewriter* dbrewDecoder = dbrew_new();

    TestRoutine testRoutine = testRoutines[(int) testCase.routineIndex];

    LLConfig config = {
        .name = "test",
        .stackSize = testCase.length >= 4 ? testCase.stackSize : 128,
        .noaliasParams = testCase.length >= 5 ? testCase.noaliasParams : 0,
        .fixFirstParam = false,
    };

    LLState* state = ll_engine_init();

    if (testCase.length >= 7 && testCase.enableUnsafePointerOptimizations)
        ll_engine_enable_unsafe_pointer_optimizations(state, true);

    LLFunction* function = ll_decode_function(dbrewDecoder, (uintptr_t) testCase.function, &config, state);

    if (function != NULL)
    {
        bool hasError = ll_function_build_ir(function, state);

        if (debug)
        {
            ll_engine_dump(state);
        }

        if (hasError)
        {
            function = NULL;
        }
        else
        {
            ll_engine_optimize(state, 3);
            // void* fn = ll_function_get_pointer(function, state);
            // ll_decode_function(dbrewDecoder, (uintptr_t) fn, &config, state);
        }
    }

    // Print correct output/behavior
    testRoutine(testCase.function);

    // Print output of LLVM rewritten function
    if (function != NULL)
    {
        void* fn = ll_function_get_pointer(function, state);

        if (debug)
        {
            // DBrew cannot decode the vectorized LLVM generated code :/
            ll_engine_disassemble(state);
            ll_decode_function(dbrewDecoder, (uintptr_t) fn, &config, state);
        }

        testRoutine(fn);

        return 0;
    }
    else
    {
        printf("LLVM failed.\n");

        return 1;
    }
}


int main(int argc, char** argv)
{
    bool debug = false;

    if (argc >= 2)
        debug = strcmp(argv[1], "--debug") == 0;

    if (testCase.routineIndex >= 0)
        return test_llvm_generation(debug);
    else
        return test_dbrew_binding(debug);
}
