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

#include <common.h>
#include <engine.h>

#include <llengine.h>

#include <llbasicblock-internal.h>
#include <llcommon.h>
#include <llcommon-internal.h>
#include <lldecoder.h>
#include <llfunction-internal.h>

/**
 * \ingroup LLEngine
 * \defgroup LLEngine Engine
 *
 * @{
 **/

/**
 * Initialize the LLVM module with the given configuration. This includes
 * setting up the MCJIT compiler and the LLVM module.
 *
 * \author Alexis Engelke
 *
 * \param config The Configuration
 * \returns The module state
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

    LLVMSetTarget(state->module, "x86_64-pc-linux-gnu"); // LLVMGetDefaultTargetTriple()
    LLVMLinkInMCJIT();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeTarget();

    LLVMInitializeMCJITCompilerOptions(&options, sizeof(options));
    options.OptLevel = 3;

    error = LLVMCreateMCJITCompilerForModule(&state->engine, state->module, &options, sizeof(options), &outerr);

    if (error)
    {
        critical("Could not setup execution engine: %s", outerr);

        free(state);

        return NULL;
    }

    state->emptyMD = LLVMMDNodeInContext(state->context, NULL, 0); //LLVMMDStringInContext(state->context, "", 0);
    state->globalOffsetBase = 0;
    state->unsafePointerOptimizations = false;

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
 * \param state The module state
 * \param enable Whether unsafe pointer optimizations can be performed.
 **/
void
ll_engine_enable_unsafe_pointer_optimizations(LLState* state, bool enable)
{
    state->unsafePointerOptimizations = enable;
}

void
ll_engine_dispose(LLState* state)
{
    // LLVMDisposeModule(state->module);
    LLVMDisposeBuilder(state->builder);
    LLVMDisposeExecutionEngine(state->engine);
    LLVMContextDispose(state->context);

    free(state);
}

// bool
// ll_engine_handle_function(LLState* state, LLFunction* function)
// {
//     char* errors;
//     bool hasError;

//     ll_function_build_ir(function, state);

//     // Verify the new function
//     // TODO: Decide whether to use LLVMVerifyFunction here instead.
//     hasError = LLVMVerifyModule(state->module, LLVMReturnStatusAction, &errors);

//     if (hasError)
//     {
//         critical("Errors while verifying the LLVM IR: %s", errors);
//         free(errors);
//         LLVMDumpModule(state->module);

//         return true;
//     }

//     ll_engine_dump(state);

//     return false;
// }

/**
 * Optimize all functions in the module.
 *
 * \author Alexis Engelke
 *
 * \param level The optimization level
 * \param state The module state
 **/
void
ll_engine_optimize(LLState* state, int level)
{
    LLVMPassManagerRef pm;

    pm = LLVMCreatePassManager();

    // LLVMAddStripSymbolsPass(pm);
    // LLVMAddStripDeadPrototypesPass(pm);

    if (level >= 1)
    {
        LLVMAddCFGSimplificationPass(pm);
        LLVMAddFunctionAttrsPass(pm);
        LLVMAddInstructionCombiningPass(pm);
        LLVMAddGVNPass(pm);

        LLVMAddEarlyCSEPass(pm);
    }

    if (level >= 2)
    {
        LLVMAddBasicAliasAnalysisPass(pm);
        // LLVMAddScopedNoAliasAAPass(pm);
        LLVMAddTypeBasedAliasAnalysisPass(pm);

        LLVMAddDeadStoreEliminationPass(pm);
        // LLVMAddMergedLoadStoreMotionPass(pm);

        // LLVMAddConstantMergePass(pm);
        // LLVMAddArgumentPromotionPass(pm);

        // LLVMAddDeadArgEliminationPass(pm);
        // LLVMAddIPConstantPropagationPass(pm);
        // LLVMAddCorrelatedValuePropagationPass(pm);
        // LLVMAddConstantPropagationPass(pm);

        LLVMAddPromoteMemoryToRegisterPass(pm);

        // LLVMAddFunctionInliningPass(pm);
        // LLVMAddAlwaysInlinerPass(pm);

        // LLVMAddGlobalOptimizerPass(pm);

        LLVMAddIndVarSimplifyPass(pm);

        LLVMAddLoopDeletionPass(pm);
        LLVMAddLoopUnrollPass(pm);
        LLVMAddLoopRerollPass(pm);
        LLVMAddLICMPass(pm);
        LLVMAddLoopIdiomPass(pm);
        LLVMAddLoopUnswitchPass(pm);
        LLVMAddScalarReplAggregatesPassSSA(pm);

        // LLVMAddScalarizerPass(pm);

        LLVMAddReassociatePass(pm);
        // LLVMAddAlignmentFromAssumptionsPass(pm);
    }

    if (level >= 3)
    {
        LLVMAddAggressiveDCEPass(pm);
        LLVMAddBBVectorizePass(pm);
        LLVMAddSLPVectorizePass(pm);
        LLVMAddLoopVectorizePass(pm);
    }

    // Run twice: The vectorizer runs late in the pipeline, but we still want to
    // optimize the vectorized code.
    // Run thrice: GCC with -O0 produces code which needs a third run to get
    // things right and looking good.
    // while (LLVMRunPassManager(pm, state->module));
    LLVMRunPassManager(pm, state->module);
    LLVMRunPassManager(pm, state->module);
    LLVMRunPassManager(pm, state->module);
    LLVMRunPassManager(pm, state->module);

    LLVMDisposePassManager(pm);

    pm = LLVMCreatePassManager();
    LLVMAddStripSymbolsPass(pm);
    LLVMRunPassManager(pm, state->module);
    LLVMDisposePassManager(pm);

    ll_engine_dump(state);
}

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

    LLFunctionDecl decl = {
        .name = "__dbrew__",
        .address = rewriter->func
    };

    LLConfig config = {
        .stackSize = 128,
        .noaliasParams = 7,
        .fixFirstParam = false
    };

    LLFunction* function = ll_function_new(&decl, &config, state);

    for (int i = 0; i < rewriter->capBBCount; i++)
    {
        CBB* cbb = rewriter->capBB + i;
        LLBasicBlock* bb = ll_basic_block_new(cbb->dec_addr);
        bb->instrs = cbb->instr;
        bb->instrCount = cbb->count;
        bb->dbrewBB = cbb;

        cbb->generatorData = bb;

        ll_function_add_basic_block(function, bb);
    }

    for (int i = 0; i < rewriter->capBBCount; i++)
    {
        CBB* cbb = rewriter->capBB + i;
        LLBasicBlock* bb = cbb->generatorData;

        if (cbb->nextBranch != NULL)
        {
            bb->nextBranch = cbb->nextBranch->generatorData;
            ll_basic_block_add_predecessor(bb->nextBranch, bb);
        }

        if (cbb->nextFallThrough != NULL)
        {
            bb->nextFallThrough = cbb->nextFallThrough->generatorData;
            ll_basic_block_add_predecessor(bb->nextFallThrough, bb);
        }
    }

    if (ll_function_build_ir(function, state))
    {
        warn_if_reached();
        rewriter->generatedCodeAddr = 0;

        return;
    }

    ll_engine_optimize(state, 3);

    rewriter->generatedCodeAddr = (uintptr_t) ll_function_get_pointer(function, state);
    rewriter->generatedCodeSize = 0;
}

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
