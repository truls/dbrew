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
#include <llflags-internal.h>
#include <llfunction.h>
#include <llfunction-internal.h>
#include <lloperand-internal.h>
#include <llsupport-internal.h>

/**
 * \defgroup LLInstructionGP General Purpose Instructions
 * \ingroup LLInstruction
 *
 * @{
 **/

void
ll_instruction_movgp(Instr* instr, LLState* state)
{
    if (opIsGPReg(&instr->dst) && opIsGPReg(&instr->src) && opTypeWidth(&instr->dst) == 64 && opTypeWidth(&instr->src) == 64)
        ll_basic_block_rename_register(state->currentBB, instr->dst.reg, instr->src.reg, state);
    else
    {
        LLVMTypeRef targetType = LLVMIntTypeInContext(state->context, opTypeWidth(&instr->dst));
        LLVMValueRef operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);

        if (instr->type == IT_MOVZX)
            operand1 = LLVMBuildZExtOrBitCast(state->builder, operand1, targetType, "");
        else if (instr->type == IT_MOVSX)
            operand1 = LLVMBuildSExtOrBitCast(state->builder, operand1, targetType, "");

        ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, operand1, state);
    }
}

void
ll_instruction_add(Instr* instr, LLState* state)
{
    LLVMValueRef operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
    LLVMValueRef operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
    operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");

    LLVMValueRef result = LLVMBuildAdd(state->builder, operand1, operand2, "");

    if (LLVMGetIntTypeWidth(LLVMTypeOf(operand1)) == 64 && opIsReg(&instr->dst))
    {
        LLVMValueRef ptr = ll_get_register(instr->dst.reg, FACET_PTR, state);
        LLVMValueRef gep = LLVMBuildGEP(state->builder, ptr, &operand2, 1, "");

        ll_set_register(instr->dst.reg, FACET_I64, result, true, state);
        ll_set_register(instr->dst.reg, FACET_PTR, gep, false, state);
    }
    else
        ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);

    ll_flags_set_add(result, operand1, operand2, state);
}

void
ll_instruction_sub(Instr* instr, LLState* state)
{
    LLVMValueRef operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
    LLVMValueRef operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
    operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");

    LLVMValueRef result = LLVMBuildSub(state->builder, operand1, operand2, "");

    if (LLVMGetIntTypeWidth(LLVMTypeOf(operand1)) == 64 && opIsReg(&instr->dst))
    {
        LLVMValueRef sub = LLVMBuildNeg(state->builder, operand2, "");
        LLVMValueRef ptr = ll_get_register(instr->dst.reg, FACET_PTR, state);
        LLVMValueRef gep = LLVMBuildGEP(state->builder, ptr, &sub, 1, "");

        ll_set_register(instr->dst.reg, FACET_I64, result, true, state);
        ll_set_register(instr->dst.reg, FACET_PTR, gep, false, state);
    }
    else
        ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);

    ll_flags_set_sub(result, operand1, operand2, state);
}

void
ll_instruction_cmp(Instr* instr, LLState* state)
{
    LLVMValueRef operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
    LLVMValueRef operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
    operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");

    LLVMValueRef result = LLVMBuildSub(state->builder, operand1, operand2, "");

    ll_flags_set_sub(result, operand1, operand2, state);
}

void
ll_instruction_test(Instr* instr, LLState* state)
{
    LLVMValueRef operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
    LLVMValueRef operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
    operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");

    LLVMValueRef result = LLVMBuildAnd(state->builder, operand1, operand2, "");

    ll_flags_set_bit(state, result, NULL, NULL);
}

void
ll_instruction_notneg(Instr* instr, LLState* state)
{
    LLVMValueRef operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
    LLVMValueRef result = NULL;

    if (instr->type == IT_NEG)
    {
        result = LLVMBuildNeg(state->builder, operand1, "");
        ll_flags_invalidate(state);
    }
    else // IT_NOT
        result = LLVMBuildNot(state->builder, operand1, "");

    ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
}

void
ll_instruction_incdec(Instr* instr, LLState* state)
{
    LLVMValueRef operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
    LLVMValueRef operand2 = LLVMConstInt(LLVMTypeOf(operand1), 1, false);
    LLVMValueRef result = NULL;

    if (instr->type == IT_INC)
    {
        result = LLVMBuildAdd(state->builder, operand1, operand2, "");
        ll_flags_set_inc(result, operand1, state);
    }
    else // IT_DEC
    {
        result = LLVMBuildSub(state->builder, operand1, operand2, "");
        ll_flags_set_dec(result, operand1, state);
    }

    ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
}

void
ll_instruction_mul(Instr* instr, LLState* state)
{
    LLVMValueRef operand1;
    LLVMValueRef operand2;
    LLVMValueRef result = NULL;

    if (instr->form == OF_1) // This covers IT_MUL as well
    {
        LLVMOpcode ext = instr->type == IT_IMUL ? LLVMSExt : LLVMZExt;
        LLVMTypeRef targetHalfType = LLVMIntTypeInContext(state->context, opTypeWidth(&instr->dst));
        LLVMTypeRef targetType = LLVMIntTypeInContext(state->context, opTypeWidth(&instr->dst) * 2);
        Operand* regOp = getRegOp(getReg(getGPRegType(opValType(&instr->dst)), RI_A));

        operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
        operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, regOp, state);

        operand1 = LLVMBuildCast(state->builder, ext, operand1, targetType, "");
        operand2 = LLVMBuildCast(state->builder, ext, operand2, targetType, "");

        result = LLVMBuildMul(state->builder, operand1, operand2, "");

        if (opTypeWidth(&instr->dst) == 8)
        {
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, getRegOp(getReg(RT_GP16, RI_A)), REG_DEFAULT, result, state);
        }
        else
        {
            LLVMValueRef resultA = LLVMBuildTrunc(state->builder, result, targetHalfType, "");
            LLVMValueRef resultD = LLVMBuildLShr(state->builder, result, LLVMConstInt(targetType, opTypeWidth(&instr->dst), false), "");
            resultD = LLVMBuildTrunc(state->builder, resultD, targetHalfType, "");

            regOp = getRegOp(getReg(getGPRegType(opValType(&instr->dst)), RI_A));
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, regOp, REG_DEFAULT, resultA, state);

            regOp = getRegOp(getReg(getGPRegType(opValType(&instr->dst)), RI_D));
            ll_operand_store(OP_SI, ALIGN_MAXIMUM, regOp, REG_DEFAULT, resultD, state);
        }
    }
    else if (instr->form == OF_2)
    {
        operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
        operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
        operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
        result = LLVMBuildMul(state->builder, operand1, operand2, "");
        ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
    }
    else if (instr->form == OF_3)
    {
        operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
        operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src2, state);
        operand2 = LLVMBuildSExtOrBitCast(state->builder, operand2, LLVMTypeOf(operand1), "");
        result = LLVMBuildMul(state->builder, operand1, operand2, "");
        ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
    }
    else
    {
        warn_if_reached();
    }
}

void
ll_instruction_lea(Instr* instr, LLState* state)
{
    LLVMTypeRef i8 = LLVMInt8TypeInContext(state->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);
    LLVMTypeRef targetType = LLVMIntTypeInContext(state->context, opTypeWidth(&instr->dst));
    LLVMTypeRef pi8 = LLVMPointerType(i8, 0);

    if (!opIsInd(&(instr->src)))
        warn_if_reached();
    if (!opIsReg(&instr->dst))
        warn_if_reached();

    LLVMValueRef result = ll_operand_get_address(OP_SI, &instr->src, state);
    result = LLVMBuildPointerCast(state->builder, result, pi8, "");

    LLVMValueRef base = LLVMConstInt(i64, instr->src.val, false);

    if (instr->src.reg.rt != RT_None)
        base = LLVMBuildAdd(state->builder, base, ll_get_register(instr->src.reg, FACET_I64, state), "");

    if (instr->src.scale != 0)
    {
        LLVMValueRef offset = ll_get_register(instr->src.ireg, FACET_I64, state);
        offset = LLVMBuildMul(state->builder, offset, LLVMConstInt(i64, instr->src.scale, false), "");
        base = LLVMBuildAdd(state->builder, base, offset, "");
    }

    base = LLVMBuildTruncOrBitCast(state->builder, base, targetType, "");
    ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, base, state);

    if (instr->dst.reg.rt == RT_GP64)
        ll_set_register(instr->dst.reg, FACET_PTR, result, false, state);
}

void
ll_instruction_cmov(Instr* instr, LLState* state)
{
    LLVMValueRef cond = ll_flags_condition(instr->type, IT_CMOVO, state);
    LLVMValueRef operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->src, state);
    LLVMValueRef operand2 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
    LLVMValueRef result = LLVMBuildSelect(state->builder, cond, operand1, operand2, "");
    ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
}

void
ll_instruction_setcc(Instr* instr, LLState* state)
{
    LLVMTypeRef i8 = LLVMInt8TypeInContext(state->context);
    LLVMValueRef cond = ll_flags_condition(instr->type, IT_SETO, state);
    LLVMValueRef result = LLVMBuildZExtOrBitCast(state->builder, cond, i8, "");
    ll_operand_store(OP_SI, ALIGN_MAXIMUM, &instr->dst, REG_DEFAULT, result, state);
}

void
ll_instruction_cdqe(Instr* instr, LLState* state)
{
    LLVMValueRef operand1 = ll_operand_load(OP_SI, ALIGN_MAXIMUM, getRegOp(getReg(RT_GP32, RI_A)), state);
    ll_operand_store(OP_SI, ALIGN_MAXIMUM, getRegOp(getReg(RT_GP64, RI_A)), REG_DEFAULT, operand1, state);

    (void) instr;
}

/**
 * @}
 **/
