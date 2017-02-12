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
 * \defgroup LLInstructionStack Push/Pop/Leave Instructions
 * \ingroup LLInstruction
 *
 * @{
 **/

void
ll_instruction_stack(Instr* instr, LLState* state)
{
    LLVMTypeRef i8 = LLVMInt8TypeInContext(state->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(state->context);
    LLVMTypeRef pi8 = LLVMPointerType(i8, 0);
    LLVMTypeRef pi64 = LLVMPointerType(i64, 0);

    // In case of a leave instruction, we basically pop the new base pointer
    // from RBP and store the new value as stack pointer.
    RegIndex spRegIndex = instr->type == IT_LEAVE ? RI_BP : RI_SP;
    LLVMValueRef spReg = ll_get_register(getReg(RT_GP64, spRegIndex), FACET_PTR, state);
    LLVMValueRef sp = LLVMBuildPointerCast(state->builder, spReg, pi64, "");
    LLVMValueRef newSp = NULL;

    if (instr->type == IT_PUSH)
    {
        // Decrement Stack Pointer via a GEP instruction
        LLVMValueRef constSub = LLVMConstInt(i64, -1, false);
        newSp = LLVMBuildGEP(state->builder, sp, &constSub, 1, "");

        LLVMValueRef value = ll_operand_load(OP_SI, ALIGN_MAXIMUM, &instr->dst, state);
        value = LLVMBuildSExtOrBitCast(state->builder, value, i64, "");
        LLVMBuildStore(state->builder, value, newSp);
    }
    else if (instr->type == IT_POP || instr->type == IT_LEAVE)
    {
        Operand* operand = instr->type == IT_LEAVE
            ? getRegOp(getReg(RT_GP64, RI_BP))
            : &instr->dst;

        LLVMValueRef value = LLVMBuildLoad(state->builder, sp, "");
        ll_operand_store(OP_SI, ALIGN_MAXIMUM, operand, REG_DEFAULT, value, state);

        // Advance Stack pointer via a GEP
        LLVMValueRef constAdd = LLVMConstInt(i64, 1, false);
        newSp = LLVMBuildGEP(state->builder, sp, &constAdd, 1, "");
    }
    else
        warn_if_reached();

    // Cast back to int for register store
    LLVMValueRef newSpReg = LLVMBuildPointerCast(state->builder, newSp, pi8, "");
    LLVMSetMetadata(newSpReg, LLVMGetMDKindIDInContext(state->context, "asm.reg.rsp", 11), state->emptyMD);

    ll_set_register(getReg(RT_GP64, RI_SP), FACET_PTR, newSpReg, true, state);
}

/**
 * @}
 **/
