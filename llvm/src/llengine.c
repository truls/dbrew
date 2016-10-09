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

/**
 * \file
 **/

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Transforms/IPO.h>
#include <llvm-c/Transforms/Scalar.h>
#include <llvm-c/Transforms/Vectorize.h>
#include <llvm-c/Transforms/PassManagerBuilder.h>

#include <common.h>
#include <engine.h>

#include <llengine.h>

#include <llbasicblock-internal.h>
#include <llcommon.h>
#include <llcommon-internal.h>
#include <lldecoder.h>
#include <llfunction.h>
#include <llfunction-internal.h>
#include <llsupport-internal.h>

/**
 * \defgroup LLEngine Engine
 * \brief Common public APIs and State management
 *
 * @{
 **/

/**
 * Initialize the LLVM module with the given configuration. This includes
 * setting up the MCJIT compiler and the LLVM module.
 *
 * \author Alexis Engelke
 *
 * \returns A new module
 **/
LLState*
ll_engine_init(void)
{
    LLState* state;

    struct LLVMMCJITCompilerOptions options;
    char* outerr = NULL;
    bool error;

    state = malloc(sizeof(LLState));
    state->context = LLVMContextCreate();
    state->module = LLVMModuleCreateWithNameInContext("<llengine>", state->context);
    state->builder = LLVMCreateBuilderInContext(state->context);
    state->functionCount = 0;
    state->functionsAllocated = 0;
    state->functions = NULL;

    LLVMSetTarget(state->module, "x86_64-pc-linux-gnu"); // LLVMGetDefaultTargetTriple()
    LLVMLinkInMCJIT();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeTarget();

    LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
    options.OptLevel = 3;

    error = LLVMCreateMCJITCompilerForModule(&state->engine, state->module, &options, sizeof(options), &outerr);

    if (error)
    {
        printf("CRITICAL Could not setup execution engine: %s", outerr);

        LLVMDisposeMessage(outerr);

        free(state);

        return NULL;
    }

    state->emptyMD = LLVMMDNodeInContext(state->context, NULL, 0); //LLVMMDStringInContext(state->context, "", 0);
    state->globalOffsetBase = 0;
    state->enableUnsafePointerOptimizations = false;
    state->enableOverflowIntrinsics = false;
    state->enableFastMath = false;

    return state;
}

/**
 * Enable unsafe pointer optimizations for arithmetic operations. This leads to
 * further optimizations when handling pointers. However, for integer operations
 * less optimization gets applied. Furthermore, if the program relies on the
 * behavior of integers, this must be turned off, as pointer arithmetics have
 * an undefined overflow behavior. As a consequence, these optimizations are
 * disabled by default.
 *
 * This function must be called before the IR of the function is built.
 *
 * \author Alexis Engelke
 *
 * \param state The module state
 * \param enable Whether unsafe pointer optimizations can be performed.
 **/
void
ll_engine_enable_unsafe_pointer_optimizations(LLState* state, bool enable)
{
    state->enableUnsafePointerOptimizations = enable;
}

/**
 * Enable the usage of overflow intrinsics instead of bitwise operations when
 * setting the overflow flag. For dynamic values this leads to better code which
 * relies on the overflow flag again. However, immediate values are not folded
 * when they are guaranteed to overflow.
 *
 * This function must be called before the IR of the function is built.
 *
 * \author Alexis Engelke
 *
 * \param state The module state
 * \param enable Whether overflow intrinsics shall be used
 **/
void
ll_engine_enable_overflow_intrinsics(LLState* state, bool enable)
{
    state->enableOverflowIntrinsics = enable;
}

/**
 * Enable unsafe floating-point optimizations, similar to -ffast-math.
 *
 * This function must be called before the IR of the function is built.
 *
 * \author Alexis Engelke
 *
 * \param state The module state
 * \param enable Whether unsafe floating-point optimizations may be performed
 **/
void
ll_engine_enable_fast_math(LLState* state, bool enable)
{
    state->enableFastMath = enable;
}

/**
 * Dispose an engine. The functions generated will not be usable any longer.
 *
 * \author Alexis Engelke
 *
 * \param state The module state
 **/
void
ll_engine_dispose(LLState* state)
{
    // LLVMDisposeModule(state->module);
    LLVMDisposeBuilder(state->builder);
    LLVMDisposeExecutionEngine(state->engine);
    LLVMContextDispose(state->context);

    free(state);
}

/**
 * Optimize all functions in the module.
 *
 * \author Alexis Engelke
 *
 * \param state The module state
 * \param level The optimization level
 **/
void
ll_engine_optimize(LLState* state, int level)
{
    LLVMPassManagerRef pm = LLVMCreatePassManager();
    LLVMPassManagerBuilderRef pmb = LLVMPassManagerBuilderCreate();

    // Run inliner early.
    LLVMAddAlwaysInlinerPass(pm);
    LLVMRunPassManager(pm, state->module);

    LLVMPassManagerBuilderSetOptLevel(pmb, level);
    ll_support_pass_manager_builder_set_enable_vectorize(pmb, level >= 3);

    LLVMPassManagerBuilderPopulateModulePassManager(pmb, pm);
    LLVMPassManagerBuilderDispose(pmb);

    // Add clean-up passes
    LLVMAddStripSymbolsPass(pm);
    LLVMAddStripDeadPrototypesPass(pm);

    LLVMRunPassManager(pm, state->module);

    LLVMDisposePassManager(pm);
}

/**
 * Dump the LLVM IR of the module.
 *
 * \author Alexis Engelke
 *
 * \param state The module state
 **/
void
ll_engine_dump(LLState* state)
{
    char* module = LLVMPrintModuleToString(state->module);

    puts(module);

    LLVMDisposeMessage(module);
}

void
ll_engine_disassemble(LLState* state)
{
    FILE* llc = popen("llc -filetype=asm", "w");

    LLVMWriteBitcodeToFD(state->module, fileno(llc), false, false);

    pclose(llc);
}

/**
 * Code generation back-end for DBrew.
 *
 * \author Alexis Engelke
 *
 * \param rewriter The DBrew rewriter
 **/
void
dbrew_llvm_backend(Rewriter* rewriter)
{
    LLState* state = ll_engine_init();

    LLConfig config = {
        .stackSize = 128,
        .signature = 026, // 6 pointer params, returns i64
        .name = "__dbrew__"
    };

    LLFunction* function = ll_function_new_definition(rewriter->func, &config, state);

    for (int i = 0; i < rewriter->capBBCount; i++)
    {
        CBB* cbb = rewriter->capBB + i;
        LLBasicBlock* bb = ll_basic_block_new_from_cbb(cbb);

        cbb->generatorData = bb;

        ll_function_add_basic_block(function, bb);
    }

    for (int i = 0; i < rewriter->capBBCount; i++)
    {
        CBB* cbb = rewriter->capBB + i;
        LLBasicBlock* bb = cbb->generatorData;
        LLBasicBlock* branch = NULL;
        LLBasicBlock* fallThrough = NULL;

        if (cbb->nextBranch != NULL)
            branch = cbb->nextBranch->generatorData;
        if (cbb->nextFallThrough != NULL)
            fallThrough = cbb->nextFallThrough->generatorData;

        ll_basic_block_add_branches(bb, branch, fallThrough);
    }

    if (ll_function_build_ir(function, state))
    {
        warn_if_reached();
        rewriter->generatedCodeAddr = 0;

        return;
    }

    ll_engine_optimize(state, 3);

    if (rewriter->showOptSteps)
        ll_engine_dump(state);

    rewriter->generatedCodeAddr = (uintptr_t) ll_function_get_pointer(function, state);
    rewriter->generatedCodeSize = 0;
}

/**
 * Rewrite a function using DBrew and the LLVM optimization and code generation.
 *
 * \author Alexis Engelke
 *
 * \param r The DBrew rewriter
 **/
uintptr_t
dbrew_llvm_rewrite(Rewriter* r, ...)
{
    va_list argptr;

    va_start(argptr, r);
    vEmulateAndCapture(r, argptr);
    va_end(argptr);

    // runOptsOnCaptured(r);
    dbrew_llvm_backend(r);

    return r->generatedCodeAddr;
}

/**
 * @}
 **/
