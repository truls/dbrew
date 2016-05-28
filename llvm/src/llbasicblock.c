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

#include <llbasicblock-internal.h>

#include <llcommon.h>
#include <llcommon-internal.h>
#include <llflags-internal.h>
#include <llinstruction-internal.h>
#include <llfunction-internal.h>

/**
 * \ingroup LLBasicBlock
 * \defgroup LLBasicBlock Basic Block
 *
 * @{
 **/


LLBasicBlock*
ll_basic_block_new(uintptr_t address)
{
    LLBasicBlock* bb;

    bb = malloc(sizeof(LLBasicBlock));
    bb->instrCount = 0;
    bb->address = address;
    bb->llvmBB = NULL;
    bb->dbrewBB = NULL;
    bb->nextBranch = NULL;
    bb->nextFallThrough = NULL;
    bb->predCount = 0;
    bb->predsAllocated = 0;

    return bb;
}

void
ll_basic_block_dispose(LLBasicBlock* bb)
{
    if (bb->predsAllocated != 0)
        free(bb->preds);

    free(bb);
}

void
ll_basic_block_declare(LLBasicBlock* bb, LLState* state)
{
    if (bb->llvmBB != NULL)
        return;

    bb->llvmBB = LLVMAppendBasicBlockInContext(state->context, state->currentFunction->decl.llvmFunction, "");
}

void
ll_basic_block_add_predecessor(LLBasicBlock* bb, LLBasicBlock* pred)
{
    if (bb->predsAllocated == 0)
    {
        bb->preds = malloc(sizeof(LLBasicBlock*) * 10);
        bb->predsAllocated = 10;

        if (bb->preds == NULL)
            warn_if_reached();
    }
    else if (bb->predsAllocated == bb->predCount)
    {
        bb->preds = realloc(bb->preds, sizeof(LLBasicBlock*) * bb->predsAllocated * 2);
        bb->predsAllocated *= 2;

        if (bb->preds == NULL)
            warn_if_reached();
    }

    bb->preds[bb->predCount] = pred;
    bb->predCount++;
}

/**
 * Split a basic block into two blocks. The second part of the block at the
 * instruction at the splitIndex is returned.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \param splitIndex The instruction where to split the block
 * \param state The engine state
 * \returns The part of the block which has been split off
 **/
LLBasicBlock*
ll_basic_block_split(LLBasicBlock* bb, size_t splitIndex, LLState* state)
{
    uintptr_t address = bb->instrs[splitIndex].addr;

    LLBasicBlock* newBB = ll_basic_block_new(address);
    newBB->instrs = bb->instrs + splitIndex;
    newBB->instrCount = bb->instrCount - splitIndex;
    newBB->nextBranch = bb->nextBranch;
    newBB->nextFallThrough = bb->nextFallThrough;

    bb->instrCount = splitIndex;
    bb->nextFallThrough = newBB;
    bb->nextBranch = NULL;

    // Update all predecessor links
    LLBasicBlock** bbs = state->currentFunction->bbs;
    for (size_t i = 0; i < state->currentFunction->bbCount; i++)
    {
        LLBasicBlock* otherBB = bbs[i];

        for (size_t j = 0; j < otherBB->predCount; j++)
        {
            if (otherBB->preds[j] == bb)
            {
                otherBB->preds[j] = newBB;
            }
        }
    }

    ll_basic_block_add_predecessor(newBB, bb);
    ll_function_add_basic_block(state->currentFunction, newBB);

    return newBB;
}

void
ll_basic_block_build_ir(LLBasicBlock* bb, LLState* state)
{
    LLVMValueRef phiNode;

    state->currentBB = bb;

    LLVMPositionBuilderAtEnd(state->builder, bb->llvmBB);

    for (int i = 0; i < Reg_Max - Reg_AX; i++)
    {
        int length = i < Reg_X0 - Reg_AX ? 64 : LL_VECTOR_REGISTER_SIZE;
        phiNode = LLVMBuildPhi(state->builder, LLVMIntTypeInContext(state->context, length), "");

        bb->registers[i] = phiNode;
        bb->phiNodesRegisters[i] = phiNode;
    }

    for (int i = 0; i < RFLAG_Max; i++)
    {
        phiNode = LLVMBuildPhi(state->builder, LLVMInt1TypeInContext(state->context), "");

        bb->flags[i] = phiNode;
        bb->phiNodesFlags[i] = phiNode;
    }

    bb->flagCache.valid = false;

    for (size_t i = 0; i < bb->instrCount; i++)
        ll_generate_instruction(bb->instrs + i, state);

    InstrType endType = IT_None;

    if (bb->dbrewBB != NULL)
        endType = bb->dbrewBB->endType;
    else if (bb->instrCount != 0)
        endType = bb->instrs[bb->instrCount - 1].type;

    if (instrIsJcc(endType))
    {
        LLVMValueRef cond = ll_flags_condition(endType, IT_JO, state);
        LLVMBuildCondBr(state->builder, cond, bb->nextBranch->llvmBB, bb->nextFallThrough->llvmBB);
    }
    else if (endType == IT_JMP)
        LLVMBuildBr(state->builder, bb->nextBranch->llvmBB);
    else if (endType != IT_RET) // Any other instruction which is not a terminator
        LLVMBuildBr(state->builder, bb->nextFallThrough->llvmBB);
}

void
ll_basic_block_fill_phis(LLBasicBlock* bb)
{
    LLVMValueRef values[bb->predCount];
    LLVMBasicBlockRef bbs[bb->predCount];

    for (int j = 0; j < Reg_Max - Reg_AX; j++)
    {
        bool isUndef = true;

        for (size_t i = 0; i < bb->predCount; i++)
        {
            bbs[i] = bb->preds[i]->llvmBB;
            values[i] = bb->preds[i]->registers[j];

            isUndef = isUndef && (LLVMIsUndef(values[i]) || values[i] == bb->phiNodesRegisters[j]);
        }

        LLVMAddIncoming(bb->phiNodesRegisters[j], values, bbs, bb->predCount);

        // Stylistic improvement: remove phis which are actually undef
        if (isUndef || j == Reg_IP - Reg_AX)
        {
            LLVMValueRef phiNode = bb->phiNodesRegisters[j];
            LLVMValueRef undef = LLVMGetUndef(LLVMTypeOf(bb->phiNodesRegisters[j]));

            LLVMReplaceAllUsesWith(phiNode, undef);
            LLVMInstructionEraseFromParent(phiNode);

            if (bb->registers[j] == phiNode)
            {
                bb->registers[j] = undef;
            }

            bb->phiNodesRegisters[j] = undef;
        }
    }

    for (int j = 0; j < RFLAG_Max; j++)
    {
        bool isUndef = true;

        for (size_t i = 0; i < bb->predCount; i++)
        {
            bbs[i] = bb->preds[i]->llvmBB;
            values[i] = bb->preds[i]->flags[j];

            isUndef = isUndef && (LLVMIsUndef(values[i]) || values[i] == bb->phiNodesFlags[j]);
        }

        LLVMAddIncoming(bb->phiNodesFlags[j], values, bbs, bb->predCount);
    }
}

LLVMValueRef
ll_basic_block_get_register(LLBasicBlock* bb, Reg reg)
{
    return bb->registers[reg - Reg_AX];
}

void
ll_basic_block_set_register(LLBasicBlock* bb, Reg reg, LLVMValueRef value)
{
    bb->registers[reg - Reg_AX] = value;
}

LLVMValueRef
ll_basic_block_get_flag(LLBasicBlock* bb, int flag)
{
    return bb->flags[flag];
}

void
ll_basic_block_set_flag(LLBasicBlock* bb, int flag, LLVMValueRef value)
{
    bb->flags[flag] = value;
}

LLFlagCache*
ll_basic_block_get_flag_cache(LLBasicBlock* bb)
{
    return &bb->flagCache;
}

/**
 * @}
 **/
