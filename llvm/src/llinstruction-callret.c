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
#include <llvm-c/Core.h>

#include <instr.h>

#include <llinstruction-internal.h>

#include <llbasicblock-internal.h>
#include <llcommon.h>
#include <llcommon-internal.h>
#include <llfunction.h>
#include <llfunction-internal.h>
#include <lloperand-internal.h>
#include <llsupport-internal.h>

/**
 * \defgroup LLInstructionCall Call/Ret Instructions
 * \ingroup LLInstruction
 *
 * @{
 **/

void
ll_instruction_call(Instr* instr, LLState* state)
{
    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);

    uintptr_t address = 0;

    if (instr->dst.type == OT_Imm64)
        address = instr->dst.val;
    else if (opIsGPReg(&instr->dst) && instr->dst.reg.rt == RT_GP64)
    {
        LLVMValueRef value = ll_get_register(instr->dst.reg, FACET_I64, state);

        if (ll_support_is_constant_int(value))
            address = LLVMConstIntGetZExtValue(value);
    }

    if (address == 0)
        warn_if_reached();

    // Find function with corresponding address.
    LLFunction* function = NULL;

    for (size_t i = 0; i < state->functionCount; i++)
        if (state->functions[i]->address == address)
            function = state->functions[i];

    if (function == NULL)
        return;
        // warn_if_reached();

    LLVMValueRef llvmFunction = function->llvmFunction;
    LLVMAddFunctionAttr(llvmFunction, LLVMInlineHintAttribute);

    // Construct arguments.
    LLVMTypeRef fnType = LLVMGetElementType(LLVMTypeOf(llvmFunction));
    size_t argCount = LLVMCountParamTypes(fnType);

    LLVMValueRef args[argCount];
    ll_operand_construct_args(fnType, args, state);

    LLVMValueRef result = LLVMBuildCall(state->builder, llvmFunction, args, argCount, "");

    if (LLVMGetTypeKind(LLVMTypeOf(result)) == LLVMPointerTypeKind)
        result = LLVMBuildPtrToInt(state->builder, result, i64, "");
    if (LLVMTypeOf(result) != i64)
        warn_if_reached();

    // TODO: Handle return values except for i64!
    ll_set_register(getReg(RT_GP64, RI_A), FACET_I64, result, true, state);

    // Clobber registers.
    ll_clear_register(getReg(RT_GP64, RI_C), state);
    ll_clear_register(getReg(RT_GP64, RI_D), state);
    ll_clear_register(getReg(RT_GP64, RI_SI), state);
    ll_clear_register(getReg(RT_GP64, RI_DI), state);
    ll_clear_register(getReg(RT_GP64, RI_8), state);
    ll_clear_register(getReg(RT_GP64, RI_9), state);
    ll_clear_register(getReg(RT_GP64, RI_10), state);
    ll_clear_register(getReg(RT_GP64, RI_11), state);
}

void
ll_instruction_ret(Instr* instr, LLState* state)
{
    LLVMTypeRef fnType = LLVMGetElementType(LLVMTypeOf(state->currentFunction->llvmFunction));
    LLVMTypeRef retType = LLVMGetReturnType(fnType);
    LLVMTypeKind retTypeKind = LLVMGetTypeKind(retType);

    LLVMValueRef result = NULL;

    if (retTypeKind == LLVMPointerTypeKind)
        result = ll_get_register(getReg(RT_GP64, RI_A), FACET_PTR, state);
    else if (retTypeKind == LLVMIntegerTypeKind)
        // TODO: Non 64-bit integers!
        result = ll_get_register(getReg(RT_GP64, RI_A), FACET_I64, state);
    else if (retTypeKind == LLVMFloatTypeKind)
        result = ll_get_register(getReg(RT_XMM, RI_XMM0), FACET_F32, state);
    else if (retTypeKind == LLVMDoubleTypeKind)
        result = ll_get_register(getReg(RT_XMM, RI_XMM0), FACET_F64, state);
    else if (retTypeKind == LLVMVoidTypeKind)
        result = NULL;
    else
        warn_if_reached();

    if (result != NULL)
        LLVMBuildRet(state->builder, result);
    else
        LLVMBuildRetVoid(state->builder);

    (void) instr;
}

/**
 * @}
 **/
