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
#include <stdint.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

#include <llfunction-internal.h>

#include <llbasicblock-internal.h>
#include <llcommon.h>
#include <llcommon-internal.h>

/**
 * \ingroup LLFunction
 * \defgroup LLFunction Function
 *
 * @{
 **/

LLFunction*
ll_function_new(LLFunctionDecl* declParam, LLConfig* config, LLState* state)
{
    LLFunction* function;
    LLFunctionDecl* decl;

    function = malloc(sizeof(LLFunction));
    function->decl = *declParam;
    function->bbCount = 0;
    function->bbs = NULL;
    function->bbsAllocated = 0;
    function->stackSize = config->stackSize;
    function->decl.noaliasParams = config->noaliasParams;
    decl = &function->decl;

    state->currentFunction = function;

    LLVMTypeRef i1 = LLVMInt1TypeInContext(state->context);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(state->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);
    LLVMTypeRef iVec = LLVMIntTypeInContext(state->context, LL_VECTOR_REGISTER_SIZE);
    LLVMTypeRef ip = LLVMPointerType(i8, 0);

    // Construct function type and add a new function to the module
    // A function has pointer types as arguments to allow alias analysis for the
    // stack leading to huge improvements. Additionally, arguments can now be
    // marked as noalias.
    LLVMTypeRef paramTypes[6] = { ip, ip, ip, ip, ip, ip };
    LLVMTypeRef functionType = LLVMFunctionType(i64, paramTypes, 6, false);
    decl->llvmFunction = LLVMAddFunction(state->module, decl->name, functionType);

    LLBasicBlock* initialBB = ll_basic_block_new(decl->address);
    ll_basic_block_declare(initialBB, state);

    // Position IR builder at a new basic block in the function
    LLVMPositionBuilderAtEnd(state->builder, initialBB->llvmBB);


    // Iterate over the parameters to initialize the registers.
    LLVMValueRef params = LLVMGetFirstParam(decl->llvmFunction);

    // Set all registers to undef first.
    for (int i = 0; i < 55; i++)
    {
        LLVMTypeRef type = i < 17 ? i64 : i < 49 ? iVec : i1;
        initialBB->registers[i] = LLVMGetUndef(type);
    }

    static Reg paramRegisters[6] = { Reg_DI, Reg_SI, Reg_DX, Reg_CX, Reg_8, Reg_9 };
    for (int i = 0; i < 6; i++)
    {
        LLVMValueRef intValue = LLVMBuildPtrToInt(state->builder, params, i64, "");

        initialBB->registers[paramRegisters[i] - Reg_AX] = intValue;

        if (config->noaliasParams & (1 << i))
        {
            LLVMAddAttribute(params, LLVMNoAliasAttribute);
        }

        params = LLVMGetNextParam(params);
    }

    if (config->fixFirstParam)
    {
        // Last check is for sanity.
        if (config->firstParamLength != 0 && config->firstParamLength < 0x200)
        {
            LLVMTypeRef arrayType = LLVMArrayType(i64, config->firstParamLength / 8);
            LLVMValueRef qwords[config->firstParamLength / 8];

            uint64_t* data = (uint64_t*) config->firstParam;
            for (size_t i = 0; i < config->firstParamLength / 8; i++)
                qwords[i] = LLVMConstInt(i64, data[i], false);

            LLVMValueRef global = LLVMAddGlobal(state->module, arrayType, "globalParam0");
            LLVMSetGlobalConstant(global, true);
            LLVMSetLinkage(global, LLVMPrivateLinkage);
            LLVMSetInitializer(global, LLVMConstArray(arrayType, qwords, config->firstParamLength / 8));

            initialBB->registers[Reg_DI - Reg_AX] = LLVMBuildPtrToInt(state->builder, global, i64, "");
        }
        else
            initialBB->registers[Reg_DI - Reg_AX] = LLVMConstInt(i64, config->firstParam, false);
    }

    // Setup virtual stack
    LLVMValueRef stackSize = LLVMConstInt(i64, config->stackSize, false);
    LLVMValueRef stack = LLVMBuildArrayAlloca(state->builder, i8, stackSize, "");
    LLVMValueRef sp = LLVMBuildGEP(state->builder, stack, &stackSize, 1, "");
    initialBB->registers[Reg_SP - Reg_AX] = LLVMBuildPtrToInt(state->builder, sp, i64, "sp");

    LLVMSetAlignment(stack, 16);

    function->initialBB = initialBB;

    return function;
}

void
ll_function_dispose(LLFunction* function)
{
    if (function->bbsAllocated != 0)
    {
        for (size_t i = 0; i < function->bbCount; i++)
            ll_basic_block_dispose(function->bbs[i]);

        free(function->bbs);
    }

    free(function);
}

void
ll_function_add_basic_block(LLFunction* function, LLBasicBlock* bb)
{
    if (function->bbsAllocated == 0)
    {
        function->bbs = malloc(sizeof(LLBasicBlock*) * 100);
        function->bbsAllocated = 100;

        if (function->bbs == NULL)
            warn_if_reached();

        ll_basic_block_add_predecessor(bb, function->initialBB);
    }
    else if (function->bbsAllocated == function->bbCount)
    {
        function->bbs = realloc(function->bbs, sizeof(LLBasicBlock*) * function->bbsAllocated * 2);
        function->bbsAllocated *= 2;

        if (function->bbs == NULL)
            warn_if_reached();
    }

    function->bbs[function->bbCount] = bb;
    function->bbCount++;
}

bool
ll_function_build_ir(LLFunction* function, LLState* state)
{
    size_t bbCount = function->bbCount;

    state->currentFunction = function;

    for (size_t i = 0; i < bbCount; i++)
    {
        ll_basic_block_declare(function->bbs[i], state);
    }

    LLVMPositionBuilderAtEnd(state->builder, function->initialBB->llvmBB);
    LLVMBuildBr(state->builder, function->bbs[0]->llvmBB);

    for (size_t i = 0; i < bbCount; i++)
    {
        ll_basic_block_build_ir(function->bbs[i], state);
    }

    for (size_t i = 0; i < bbCount; i++)
    {
        if (function->bbs[i]->predCount == 0)
        {
            LLVMRemoveBasicBlockFromParent(function->bbs[i]->llvmBB);
        }
        else
        {
            ll_basic_block_fill_phis(function->bbs[i]);
        }
    }

    // LLVMDumpModule(state->module);
    // __asm__("int3");
    bool error = LLVMVerifyFunction(function->decl.llvmFunction, LLVMPrintMessageAction);

    return error;
}

void*
ll_function_get_pointer(LLFunction* function, LLState* state)
{
    return LLVMGetPointerToGlobal(state->engine, function->decl.llvmFunction);
}

/**
 * @}
 **/
