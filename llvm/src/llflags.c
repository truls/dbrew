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
#include <printer.h>

#include <llflags-internal.h>

#include <llbasicblock-internal.h>
#include <llcommon.h>
#include <llcommon-internal.h>
#include <llfunction-internal.h>
#include <llsupport.h>

/**
 * \ingroup LLFlags
 * \defgroup LLFlags Flags
 *
 * Computation of flags ported from https://github.com/trailofbits/mcsema .
 *
 * @{
 **/


LLVMValueRef
ll_flags_condition(InstrType type, InstrType base, LLState* state)
{
    int condition = type - base;
    int conditionType = condition >> 1;
    bool negate = condition & 1;

    LLVMValueRef result;
    LLFlagCache* flagCache = &state->currentBB->flagCache;

    switch (conditionType)
    {
        case 0: // JO / JNO
            result = state->currentBB->registers[RFLAG_OF];
            break;
        case 1: // JC / JNC
            result = state->currentBB->registers[RFLAG_CF];
            break;
        case 2: // JZ / JNZ
            result = state->currentBB->registers[RFLAG_ZF];
            break;
        case 3: // JBE / JA
            if (flagCache->valid)
            {
                result = LLVMBuildICmp(state->builder, LLVMIntULE, flagCache->operand1, flagCache->operand2, "");
            }
            else
            {
                result = LLVMBuildOr(state->builder, state->currentBB->registers[RFLAG_CF], state->currentBB->registers[RFLAG_ZF], "");
            }
            break;
        case 4: // JS / JNS
            result = state->currentBB->registers[RFLAG_SF];
            break;
        case 5: // JP / JNP
            result = state->currentBB->registers[RFLAG_PF];
            break;
        case 6: // JL / JGE
            if (flagCache->valid)
            {
                result = LLVMBuildICmp(state->builder, LLVMIntSLT, flagCache->operand1, flagCache->operand2, "");
            }
            else
            {
                result = LLVMBuildICmp(state->builder, LLVMIntNE, state->currentBB->registers[RFLAG_SF], state->currentBB->registers[RFLAG_OF], "");
            }
            break;
        case 7: // JLE / JG
            if (flagCache->valid)
            {
                result = LLVMBuildICmp(state->builder, LLVMIntSLE, flagCache->operand1, flagCache->operand2, "");
            }
            else
            {
                result = LLVMBuildICmp(state->builder, LLVMIntNE, state->currentBB->registers[RFLAG_SF], state->currentBB->registers[RFLAG_OF], "");
                result = LLVMBuildOr(state->builder, result, state->currentBB->registers[RFLAG_ZF], "");
            }
            break;
        default:
            result = NULL;
            break;
    }

    if (negate)
    {
        result = LLVMBuildNot(state->builder, result, "");
    }

    return result;
}

/*
 * Compute the RFLAGS of a subtraction
 *
 * Credits to https://github.com/trailofbits/mcsema
 */

static void
ll_flags_set_af(LLVMValueRef result, LLVMValueRef lhs, LLVMValueRef rhs, LLState* state)
{
    LLVMValueRef xor1 = LLVMBuildXor(state->builder, lhs, result, "");
    LLVMValueRef xor2 = LLVMBuildXor(state->builder, xor1, rhs, "");
    LLVMValueRef and1 = LLVMBuildAnd(state->builder, xor2, LLVMConstInt(LLVMTypeOf(result), 16, false), "");
    state->currentBB->registers[RFLAG_AF] = LLVMBuildICmp(state->builder, LLVMIntNE, and1, LLVMConstInt(LLVMTypeOf(result), 0, false), "");

    LLVMSetMetadata(state->currentBB->registers[RFLAG_AF], LLVMGetMDKindIDInContext(state->context, "asm.flag.af", 11), state->emptyMD);
}

static void
ll_flags_set_zf(LLVMValueRef result, LLState* state)
{
    state->currentBB->registers[RFLAG_ZF] = LLVMBuildICmp(state->builder, LLVMIntEQ, result, LLVMConstInt(LLVMTypeOf(result), 0, false), "");

    LLVMSetMetadata(state->currentBB->registers[RFLAG_ZF], LLVMGetMDKindIDInContext(state->context, "asm.flag.zf", 11), state->emptyMD);
}

static void
ll_flags_set_sf(LLVMValueRef result, LLState* state)
{
    int width = LLVMGetIntTypeWidth(LLVMTypeOf(result));
    LLVMTypeRef intType = LLVMTypeOf(result);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(state->context);

    LLVMValueRef msb = LLVMBuildLShr(state->builder, result, LLVMConstInt(intType, width - 1, false), "");
    state->currentBB->registers[RFLAG_SF] = LLVMBuildTrunc(state->builder, msb, i1, "");

    LLVMSetMetadata(state->currentBB->registers[RFLAG_SF], LLVMGetMDKindIDInContext(state->context, "asm.flag.sf", 11), state->emptyMD);
}

static void
ll_flags_set_of_sub(LLVMValueRef result, LLVMValueRef lhs, LLVMValueRef rhs, LLState* state)
{
    int width = LLVMGetIntTypeWidth(LLVMTypeOf(result));
    LLVMTypeRef intType = LLVMTypeOf(result);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(state->context);

    LLVMValueRef xor1 = LLVMBuildXor(state->builder, lhs, result, "");
    LLVMValueRef xor2 = LLVMBuildXor(state->builder, lhs, rhs, "");
    LLVMValueRef and = LLVMBuildAnd(state->builder, xor1, xor2, "");
    LLVMValueRef overflow = LLVMBuildLShr(state->builder, and, LLVMConstInt(intType, width - 1, false), "");
    state->currentBB->registers[RFLAG_OF] = LLVMBuildTrunc(state->builder, overflow, i1, "");

    LLVMSetMetadata(state->currentBB->registers[RFLAG_OF], LLVMGetMDKindIDInContext(state->context, "asm.flag.of", 11), state->emptyMD);
}

static void
ll_flags_set_cf_sub(LLVMValueRef lhs, LLVMValueRef rhs, LLState* state)
{
    state->currentBB->registers[RFLAG_CF] = LLVMBuildICmp(state->builder, LLVMIntULT, lhs, rhs, "");

    LLVMSetMetadata(state->currentBB->registers[RFLAG_CF], LLVMGetMDKindIDInContext(state->context, "asm.flag.cf", 11), state->emptyMD);
}

static void
ll_flags_set_of_add(LLVMValueRef result, LLVMValueRef lhs, LLVMValueRef rhs, LLState* state)
{
    int width = LLVMGetIntTypeWidth(LLVMTypeOf(result));
    LLVMTypeRef intType = LLVMTypeOf(result);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(state->context);

    // TODO: Verify this
    LLVMValueRef xor1 = LLVMBuildXor(state->builder, lhs, result, "");
    LLVMValueRef xor2 = LLVMBuildXor(state->builder, lhs, rhs, "");
    LLVMValueRef not = LLVMBuildNot(state->builder, xor2, "");
    LLVMValueRef and = LLVMBuildAnd(state->builder, xor1, not, "");
    LLVMValueRef overflow = LLVMBuildLShr(state->builder, and, LLVMConstInt(intType, width - 1, false), "");
    state->currentBB->registers[RFLAG_OF] = LLVMBuildTrunc(state->builder, overflow, i1, "");

    LLVMSetMetadata(state->currentBB->registers[RFLAG_OF], LLVMGetMDKindIDInContext(state->context, "asm.flag.of", 11), state->emptyMD);
}

static void
ll_flags_set_cf_add(LLVMValueRef result, LLVMValueRef lhs, LLState* state)
{
    state->currentBB->registers[RFLAG_CF] = LLVMBuildICmp(state->builder, LLVMIntULT, result, lhs, "");

    LLVMSetMetadata(state->currentBB->registers[RFLAG_CF], LLVMGetMDKindIDInContext(state->context, "asm.flag.cf", 11), state->emptyMD);
}

static void
ll_flags_set_pf(LLVMValueRef result, LLState* state)
{
    LLVMTypeRef i1 = LLVMInt1TypeInContext(state->context);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(state->context);

    LLVMValueRef intrinsicCtpop8 = ll_support_get_intrinsic(state->module, LL_INTRINSIC_CTPOP, &i8, 1);

    LLVMValueRef arg = LLVMBuildTruncOrBitCast(state->builder, result, i8, "");
    LLVMValueRef count = LLVMBuildCall(state->builder, intrinsicCtpop8, &arg, 1, "");
    LLVMValueRef bit = LLVMBuildTruncOrBitCast(state->builder, count, i1, "");
    state->currentBB->registers[RFLAG_PF] = LLVMBuildNot(state->builder, bit, "");

    LLVMSetMetadata(state->currentBB->registers[RFLAG_PF], LLVMGetMDKindIDInContext(state->context, "asm.flag.pf", 11), state->emptyMD);
}

void
ll_flags_set_sub(LLVMValueRef result, LLVMValueRef lhs, LLVMValueRef rhs, LLState* state)
{
    ll_flags_set_af(result, lhs, rhs, state);
    ll_flags_set_zf(result, state);
    ll_flags_set_sf(result, state);
    ll_flags_set_cf_sub(lhs, rhs, state);
    ll_flags_set_of_sub(result, lhs, rhs, state);
    ll_flags_set_pf(result, state);

    LLFlagCache* flagCache = &state->currentBB->flagCache;
    flagCache->valid = true;
    flagCache->operand1 = lhs;
    flagCache->operand2 = rhs;
    flagCache->result = result;
}

void
ll_flags_set_add(LLVMValueRef result, LLVMValueRef lhs, LLVMValueRef rhs, LLState* state)
{
    ll_flags_set_af(result, lhs, rhs, state);
    ll_flags_set_zf(result, state);
    ll_flags_set_sf(result, state);
    ll_flags_set_cf_add(result, lhs, state);
    ll_flags_set_of_add(result, lhs, rhs, state);
    ll_flags_set_pf(result, state);

    LLFlagCache* flagCache = &state->currentBB->flagCache;
    flagCache->valid = false;
}

void
ll_flags_set_bit(LLVMValueRef result, LLState* state)
{
    LLVMTypeRef i1 = LLVMInt1TypeInContext(state->context);

    state->currentBB->registers[RFLAG_AF] = LLVMGetUndef(i1);
    state->currentBB->registers[RFLAG_CF] = LLVMConstInt(i1, 0, false);
    state->currentBB->registers[RFLAG_OF] = LLVMConstInt(i1, 0, false);

    ll_flags_set_zf(result, state);
    ll_flags_set_sf(result, state);
    ll_flags_set_pf(result, state);

    LLFlagCache* flagCache = &state->currentBB->flagCache;
    flagCache->valid = false;
}

void
ll_flags_invalidate(LLState* state)
{
    LLVMTypeRef i1 = LLVMInt1TypeInContext(state->context);

    state->currentBB->registers[RFLAG_AF] = LLVMGetUndef(i1);
    state->currentBB->registers[RFLAG_CF] = LLVMGetUndef(i1);
    state->currentBB->registers[RFLAG_OF] = LLVMGetUndef(i1);
    state->currentBB->registers[RFLAG_SF] = LLVMGetUndef(i1);
    state->currentBB->registers[RFLAG_ZF] = LLVMGetUndef(i1);
    state->currentBB->registers[RFLAG_PF] = LLVMGetUndef(i1);

    LLFlagCache* flagCache = &state->currentBB->flagCache;
    flagCache->valid = false;
}

/**
 * @}
 **/
