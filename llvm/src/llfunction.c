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

#include <llfunction.h>
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

static LLFunction*
ll_function_new(LLFunctionKind kind, uintptr_t address, LLState* state)
{
    LLFunction* function;

    function = malloc(sizeof(LLFunction));
    function->kind = kind;
    function->address = address;

    if (state->functionsAllocated == 0)
    {
        state->functions = malloc(sizeof(LLBasicBlock*) * 10);
        state->functionsAllocated = 10;

        if (state->functions == NULL)
            warn_if_reached();
    }
    else if (state->functionsAllocated == state->functionCount)
    {
        state->functions = realloc(state->functions, sizeof(LLBasicBlock*) * state->functionsAllocated * 2);
        state->functionsAllocated *= 2;

        if (state->functions == NULL)
            warn_if_reached();
    }

    state->functions[state->functionCount] = function;
    state->functionCount++;

    return function;
}

LLFunction*
ll_function_new_definition(uintptr_t address, LLConfig* config, LLState* state)
{
    LLFunction* function = ll_function_new(LL_FUNCTION_DEFINITION, address, state);
    function->name = config->name;
    function->u.definition.bbCount = 0;
    function->u.definition.bbs = NULL;
    function->u.definition.bbsAllocated = 0;
    function->u.definition.stackSize = config->stackSize;
    function->noaliasParams = config->noaliasParams;

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
    function->llvmFunction = LLVMAddFunction(state->module, function->name, functionType);

    LLBasicBlock* initialBB = ll_basic_block_new(function->address);
    ll_basic_block_declare(initialBB, state);

    // Position IR builder at a new basic block in the function
    LLVMPositionBuilderAtEnd(state->builder, ll_basic_block_llvm(initialBB));

    // Iterate over the parameters to initialize the registers.
    LLVMValueRef params = LLVMGetFirstParam(function->llvmFunction);

    // Set all registers to undef first.
    for (Reg i = Reg_AX; i < Reg_Max; i++)
        ll_basic_block_set_register(initialBB, i, LLVMGetUndef(i < Reg_X0 ? i64 : iVec));

    for (int i = 0; i < RFLAG_Max; i++)
        ll_basic_block_set_flag(initialBB, i, LLVMGetUndef(i1));

    static Reg paramRegisters[6] = { Reg_DI, Reg_SI, Reg_DX, Reg_CX, Reg_8, Reg_9 };
    for (int i = 0; i < 6; i++)
    {
        LLVMValueRef intValue = LLVMBuildPtrToInt(state->builder, params, i64, "");

        ll_basic_block_set_register(initialBB, paramRegisters[i], intValue);

        if (config->noaliasParams & (1 << i))
        {
            LLVMAddAttribute(params, LLVMNoAliasAttribute);
        }

        params = LLVMGetNextParam(params);
    }

    // Setup virtual stack
    LLVMValueRef stackSize = LLVMConstInt(i64, config->stackSize, false);
    LLVMValueRef stack = LLVMBuildArrayAlloca(state->builder, i8, stackSize, "");
    LLVMValueRef sp = LLVMBuildGEP(state->builder, stack, &stackSize, 1, "");
    ll_basic_block_set_register(initialBB, Reg_SP, LLVMBuildPtrToInt(state->builder, sp, i64, "sp"));

    LLVMSetAlignment(stack, 16);

    function->u.definition.initialBB = initialBB;

    return function;
}

LLFunction*
ll_function_specialize(LLFunction* base, uintptr_t index, uintptr_t value, size_t length, LLState* state)
{
    LLFunction* function = ll_function_new(LL_FUNCTION_SPECIALIZATION, 0, state);
    function->name = base->name;

    LLVMTypeRef fnType = LLVMGetElementType(LLVMTypeOf(base->llvmFunction));
    size_t paramCount = LLVMCountParamTypes(fnType);

    LLVMTypeRef paramTypes[paramCount];
    LLVMGetParamTypes(fnType, paramTypes);

    // Add alwaysinline attribute such that the optimization routine inlines the
    // base function for the best results.
    LLVMAddFunctionAttr(base->llvmFunction, LLVMAlwaysInlineAttribute);

    if (index >= paramCount)
        warn_if_reached();

    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);
    function->llvmFunction = LLVMAddFunction(state->module, function->name, fnType);

    LLVMValueRef params = LLVMGetFirstParam(function->llvmFunction);
    LLVMValueRef baseParams = LLVMGetFirstParam(base->llvmFunction);
    LLVMValueRef args[paramCount];

    LLVMValueRef fixed;

    // Last check is for sanity.
    if (length != 0 && length < 0x200)
    {
        LLVMTypeRef arrayType = LLVMArrayType(i64, length / 8);
        LLVMValueRef qwords[length / 8];

        uint64_t* data = (uint64_t*) value;
        for (size_t i = 0; i < length / 8; i++)
            qwords[i] = LLVMConstInt(i64, data[i], false);

        LLVMValueRef global = LLVMAddGlobal(state->module, arrayType, "globalParam0");
        LLVMSetGlobalConstant(global, true);
        LLVMSetLinkage(global, LLVMPrivateLinkage);
        LLVMSetInitializer(global, LLVMConstArray(arrayType, qwords, length / 8));

        fixed = LLVMBuildPointerCast(state->builder, global, paramTypes[index], "");
    }
    else
        fixed = LLVMConstPtrToInt(LLVMConstInt(i64, value, false), paramTypes[index]);

    for (uintptr_t i = 0; i < paramCount; i++)
    {
        if (i == index)
            args[i] = fixed;
        else
            args[i] = params;

        if (LLVMGetAttribute(baseParams) != 0)
            LLVMAddAttribute(params, LLVMGetAttribute(baseParams));

        params = LLVMGetNextParam(params);
        baseParams = LLVMGetNextParam(baseParams);
    }

    LLVMBasicBlockRef llvmBB = LLVMAppendBasicBlock(function->llvmFunction, "");
    LLVMPositionBuilderAtEnd(state->builder, llvmBB);

    LLVMValueRef retValue = LLVMBuildCall(state->builder, base->llvmFunction, args, paramCount, "");
    LLVMBuildRet(state->builder, retValue);

    return function;
}

void
ll_function_dispose(LLFunction* function)
{
    switch (function->kind)
    {
        case LL_FUNCTION_DEFINITION:
            if (function->u.definition.bbsAllocated != 0)
            {
                for (size_t i = 0; i < function->u.definition.bbCount; i++)
                    ll_basic_block_dispose(function->u.definition.bbs[i]);

                free(function->u.definition.bbs);
            }
            break;
        case LL_FUNCTION_DECLARATION:
        case LL_FUNCTION_SPECIALIZATION:
            break;
        default:
            warn_if_reached();
    }

    free(function);
}

void
ll_function_add_basic_block(LLFunction* function, LLBasicBlock* bb)
{
    if (function->kind != LL_FUNCTION_DEFINITION)
        warn_if_reached();

    if (function->u.definition.bbsAllocated == 0)
    {
        function->u.definition.bbs = malloc(sizeof(LLBasicBlock*) * 100);
        function->u.definition.bbsAllocated = 100;

        if (function->u.definition.bbs == NULL)
            warn_if_reached();

        ll_basic_block_add_predecessor(bb, function->u.definition.initialBB);
    }
    else if (function->u.definition.bbsAllocated == function->u.definition.bbCount)
    {
        function->u.definition.bbs = realloc(function->u.definition.bbs, sizeof(LLBasicBlock*) * function->u.definition.bbsAllocated * 2);
        function->u.definition.bbsAllocated *= 2;

        if (function->u.definition.bbs == NULL)
            warn_if_reached();
    }

    function->u.definition.bbs[function->u.definition.bbCount] = bb;
    function->u.definition.bbCount++;
}

bool
ll_function_build_ir(LLFunction* function, LLState* state)
{
    switch (function->kind)
    {
        case LL_FUNCTION_DEFINITION:
            {
                size_t bbCount = function->u.definition.bbCount;

                state->currentFunction = function;

                for (size_t i = 0; i < bbCount; i++)
                    ll_basic_block_declare(function->u.definition.bbs[i], state);

                LLVMPositionBuilderAtEnd(state->builder, ll_basic_block_llvm(function->u.definition.initialBB));
                LLVMBuildBr(state->builder, ll_basic_block_llvm(function->u.definition.bbs[0]));

                for (size_t i = 0; i < bbCount; i++)
                    ll_basic_block_build_ir(function->u.definition.bbs[i], state);

                for (size_t i = 0; i < bbCount; i++)
                    ll_basic_block_fill_phis(function->u.definition.bbs[i]);
            }
            break;
        case LL_FUNCTION_DECLARATION:
        case LL_FUNCTION_SPECIALIZATION:
            break;
        default:
            warn_if_reached();
    }

    bool error = LLVMVerifyFunction(function->llvmFunction, LLVMPrintMessageAction);

    return error;
}

void*
ll_function_get_pointer(LLFunction* function, LLState* state)
{
    return LLVMGetPointerToGlobal(state->engine, function->llvmFunction);
}

/**
 * @}
 **/
