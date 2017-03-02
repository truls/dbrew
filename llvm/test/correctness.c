/**
 * This file is part of DBrew, the dynamic binary rewriting library.
 *
 * (c) 2015-2016, Josef Weidendorfer <josef.weidendorfer@gmx.de>
 *
 * DBrew is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License (LGPL)
 * as published by the Free Software Foundation, either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * DBrew is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with DBrew.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#include <glib.h>

#include <dbrew.h>
#include <dbrew-llvm.h>


struct TestFunction {
    const char* caseName;
    void(* function)(void);
    size_t signature;
    size_t flags;
};

typedef struct TestFunction TestFunction;

static
void
test_compare_output(gconstpointer userdata)
{
    const TestFunction* testFunc = userdata;

    LLConfig config = {
        .name = "test",
        .signature = testFunc->signature,
        .stackSize = 256,
    };

    LLState* engine = ll_engine_init();
    g_assert(engine != NULL);

    Rewriter* dbrewDecoder = dbrew_new();
    LLFunction* function = ll_decode_function((uintptr_t) testFunc->function, (DecodeFunc) dbrew_decode, dbrewDecoder, &config, engine);
    g_assert(function != NULL);

    if (g_test_verbose())
        ll_engine_dump(engine);
    ll_engine_optimize(engine, 3);
    if (g_test_verbose())
        ll_engine_dump(engine);

    // C99 is sometimes weird.
    void(* fn)(void);
    *((void**) &fn) = ll_function_get_pointer(function, engine);

    size_t repetitions = 100;

    switch (testFunc->signature)
    {
        uint64_t i64_1;
        uint64_t i64_2;
        float f32_1;
        float f32_2;
        double f64_1;
        double f64_2;

        case 000:
        case 020:
            i64_1 = ((uint64_t(*)(void))fn)();
            i64_2 = ((uint64_t(*)(void))testFunc->function)();
            g_assert_cmpuint(i64_1, ==, i64_2);
            break;
        case 0221:
            for (size_t i = 0; i < repetitions; i++)
            {
                uint64_t rand = ((size_t) g_test_rand_int() << 32) | g_test_rand_int();
                i64_1 = ((uint64_t(*)(uint64_t))fn)(rand);
                i64_2 = ((uint64_t(*)(uint64_t))testFunc->function)(rand);
                g_assert_cmpuint(i64_1, ==, i64_2);
            }
            break;
        case 00002:
        case 02002:
        case 02222:
            for (size_t i = 0; i < repetitions; i++)
            {
                uint64_t rand1 = ((size_t) g_test_rand_int() << 32) | g_test_rand_int();
                uint64_t rand2 = ((size_t) g_test_rand_int() << 32) | g_test_rand_int();
                i64_1 = ((uint64_t(*)(uint64_t,uint64_t))fn)(rand1, rand2);
                i64_2 = ((uint64_t(*)(uint64_t,uint64_t))testFunc->function)(rand1, rand2);
                g_assert_cmpuint(i64_1, ==, i64_2);
            }
            break;
        case 060:
            f32_1 = ((float(*)(void))fn)();
            f32_2 = ((float(*)(void))testFunc->function)();
            g_assert_cmpfloat(f32_1, ==, f32_2);
            break;
        case 0621:
            {
                float rand = (float) g_test_rand_double_range(-1, 1);
                i64_1 = ((uint64_t(*)(float))fn)(rand);
                i64_2 = ((uint64_t(*)(float))testFunc->function)(rand);
                g_assert_cmpfloat(i64_1, ==, i64_2);
            }
            break;
        case 0661:
            {
                float rand = (float) g_test_rand_double_range(-1, 1);
                f32_1 = ((float(*)(float))fn)(rand);
                f32_2 = ((float(*)(float))testFunc->function)(rand);
                g_assert_cmpfloat(f32_1, ==, f32_2);
            }
            break;
        case 070:
            f64_1 = ((double(*)(void))fn)();
            f64_2 = ((double(*)(void))testFunc->function)();
            g_assert_cmpfloat(f64_1, ==, f64_2);
            break;
        case 0721:
            {
                double rand = g_test_rand_double_range(-1, 1);
                i64_1 = ((uint64_t(*)(double))fn)(rand);
                i64_2 = ((uint64_t(*)(double))testFunc->function)(rand);
                g_assert_cmpfloat(i64_1, ==, i64_2);
            }
            break;
        case 07722:
            {
                double rand1 = g_test_rand_double_range(-1, 1);
                double rand2 = g_test_rand_double_range(-1, 1);
                i64_1 = ((uint64_t(*)(double,double))fn)(rand1, rand2);
                i64_2 = ((uint64_t(*)(double,double))testFunc->function)(rand1, rand2);
                g_assert_cmpfloat(i64_1, ==, i64_2);
            }
            break;
        default:
            g_assert_not_reached();
    }

    ll_engine_dispose(engine);
    dbrew_free(dbrewDecoder);
}

#define ASM_DECL(name,...) extern void name(void); \
        __asm__(".intel_syntax noprefix; " G_STRINGIFY(name) ": " #__VA_ARGS__ "; .att_syntax")

#define TEST_NAMESPACE globl
#define TEST_FUNC_NAME() G_PASTE(G_PASTE(__testfunc_, TEST_NAMESPACE), __LINE__)
#define TEST_CASE(path,signature,flags,...) TEST_CASE_NAMED(path,,signature,flags, __VA_ARGS__)

#define OF 0x0800
#define SF 0x0080
#define ZF 0x0040
#define AF 0x0010
#define PF 0x0004
#define CF 0x0001
#define RET_FLAGS_MASK(mask) pushfq ; pop rax ; and eax, mask ; ret
#define RET_FLAGS RET_FLAGS_MASK(OF|SF|ZF|AF|PF|CF)

#define UNPCK(reg,val) push val ; push val ; movdqu reg, [rsp] ; add rsp, 0x10
#define RET_XMM0 sub rsp, 0x10 ; movdqu [rsp], xmm0 ; pop rax ; pop rdx ; not rdx ; xor rax, rdx ; ret
#define RET_XMM0S movd eax, xmm0 ; ret
#define RET_XMM0D movq rax, xmm0 ; ret


#define TEST_CASE_NAMED(path,name,signature,flags,...) ASM_DECL(G_PASTE(TEST_FUNC_NAME(), name), __VA_ARGS__);
#include "cases.inc"

#undef TEST_CASE_NAMED
#define TEST_CASE_NAMED(path,name,signature,flags,...) { path, G_PASTE(TEST_FUNC_NAME(),name), signature, flags },

static const TestFunction tests[] = {
#include "cases.inc"
};

int
main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);

    for (size_t i = 0; i < G_N_ELEMENTS(tests); i++)
    {
        g_test_add_data_func(tests[i].caseName, &tests[i], test_compare_output);
    }

    return g_test_run();
}
