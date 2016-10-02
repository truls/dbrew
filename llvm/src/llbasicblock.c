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
 * \defgroup LLBasicBlock Basic Block
 * \brief Representation of a basic block
 *
 * @{
 **/

struct LLBasicBlock {
    /**
     * \brief The address
     **/
    uintptr_t address;

    /**
     * \brief The instruction count
     **/
    size_t instrCount;
    /**
     * \brief The instructions
     **/
    Instr* instrs;

    /**
     * \brief The branch basic block, or NULL
     **/
    LLBasicBlock* nextBranch;
    /**
     * \brief The fall-through basic block, or NULL
     **/
    LLBasicBlock* nextFallThrough;

    // Predecessors needed for phi nodes
    /**
     * \brief The predecessor count
     **/
    size_t predCount;
    /**
     * \brief The number predecessors allocated
     **/
    size_t predsAllocated;
    /**
     * \brief The preceding basic blocks
     **/
    LLBasicBlock** preds;

    /**
     * \brief The LLVM basic block
     **/
    LLVMBasicBlockRef llvmBB;

    /**
     * \brief The DBrew CBB. NULL if the basic block is not derived from DBrew.
     **/
    CBB* dbrewBB;

    /**
     * \brief The LLVM values of the architectural general purpose registers
     *
     * The registers always store integers with 64 bits length.
     **/
    LLVMValueRef gpRegisters[RI_GPMax];

    /**
     * \brief The LLVM values of the SSE registers
     *
     * The vector length depends on #LL_VECTOR_REGISTER_SIZE.
     **/
    LLVMValueRef sseRegisters[RI_XMMMax];

    /**
     * \brief The LLVM values of the architectural general purpose registers
     **/
    LLVMValueRef flags[RFLAG_Max];

    /**
     * \brief The LLVM value of the current instruction address
     **/
    LLVMValueRef ipRegister;

    /**
     * \brief The phi nodes for the registers
     **/
    LLVMValueRef phiNodesGpRegisters[RI_GPMax];

    /**
     * \brief The phi nodes for the registers
     **/
    LLVMValueRef phiNodesSseRegisters[RI_XMMMax];

    /**
     * \brief The phi nodes for the flags
     **/
    LLVMValueRef phiNodesFlags[RFLAG_Max];

    /**
     * \brief The flag cache
     **/
    LLFlagCache flagCache;
};

/**
 * Create a new basic block.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param address The address of the basic block
 * \returns The new basic block
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

/**
 * Create a new basic block from a DBB.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param dbb The underlying DBB
 * \returns The new basic block
 **/
LLBasicBlock*
ll_basic_block_new_from_dbb(DBB* dbb)
{
    LLBasicBlock* bb = ll_basic_block_new(dbb->addr);

    bb->instrs = dbb->instr;
    bb->instrCount = dbb->count;

    return bb;
}

/**
 * Create a new basic block from a CBB.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param cbb The underlying CBB
 * \returns The new basic block
 **/
LLBasicBlock*
ll_basic_block_new_from_cbb(CBB* cbb)
{
    LLBasicBlock* bb = ll_basic_block_new(cbb->dec_addr);

    bb->instrs = cbb->instr;
    bb->instrCount = cbb->count;
    bb->dbrewBB = cbb;

    return bb;
}

/**
 * Dispose a basic block.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 **/
void
ll_basic_block_dispose(LLBasicBlock* bb)
{
    if (bb->predsAllocated != 0)
        free(bb->preds);

    free(bb);
}

/**
 * Declare a basic block in the current function.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \param state The module state
 **/
void
ll_basic_block_declare(LLBasicBlock* bb, LLState* state)
{
    if (bb->llvmBB != NULL)
        return;

    bb->llvmBB = LLVMAppendBasicBlockInContext(state->context, state->currentFunction->llvmFunction, "");
}

/**
 * Add a predecessor.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \param pred The preceding basic block
 **/
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
 * Get the LLVM value of the basic block.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \returns The LLVM basic block
 **/
LLVMBasicBlockRef
ll_basic_block_llvm(LLBasicBlock* bb)
{
    return bb->llvmBB;
}

/**
 * Find an instruction with the given address in the basic block.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \param address The address of an instruction
 * \returns The index of the instruction, or -1 if no instruction with the given
 * address exists.
 **/
long
ll_basic_block_find_address(LLBasicBlock* bb, uintptr_t address)
{
    for (size_t j = 0; j < bb->instrCount; j++)
        if (bb->instrs[j].addr == address)
            return j;

    return -1;
}

/**
 * Add branches to the basic block. This also registers them as predecessors.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \param branch The active branch, or NULL
 * \param fallThrough The fall-through branch, or NULL
 **/
void
ll_basic_block_add_branches(LLBasicBlock* bb, LLBasicBlock* branch, LLBasicBlock* fallThrough)
{
    if (branch != NULL)
    {
        ll_basic_block_add_predecessor(branch, bb);
        bb->nextBranch = branch;
    }

    if (fallThrough != NULL)
    {
        ll_basic_block_add_predecessor(fallThrough, bb);
        bb->nextFallThrough = fallThrough;
    }
}

/**
 * Truncate the basic block to the given number of instructions.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \param splitIndex The instruction where to cut the block
 **/
void
ll_basic_block_truncate(LLBasicBlock* bb, size_t splitIndex)
{
    bb->instrCount = splitIndex;
    bb->nextFallThrough = NULL;
    bb->nextBranch = NULL;
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
 * \param state The module state
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
    LLBasicBlock** bbs = state->currentFunction->u.definition.bbs;
    for (size_t i = 0; i < state->currentFunction->u.definition.bbCount; i++)
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

/**
 * Build the LLVM IR.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \param state The module state
 **/
void
ll_basic_block_build_ir(LLBasicBlock* bb, LLState* state)
{
    if (bb->predCount == 0)
    {
        LLVMRemoveBasicBlockFromParent(bb->llvmBB);
        return;
    }

    LLVMValueRef phiNode;

    state->currentBB = bb;

    LLVMPositionBuilderAtEnd(state->builder, bb->llvmBB);

    for (int i = 0; i < RI_GPMax; i++)
    {
        phiNode = LLVMBuildPhi(state->builder, LLVMInt64TypeInContext(state->context), "");

        bb->gpRegisters[i] = phiNode;
        bb->phiNodesGpRegisters[i] = phiNode;
    }

    for (int i = 0; i < RI_XMMMax; i++)
    {
        phiNode = LLVMBuildPhi(state->builder, LLVMIntTypeInContext(state->context, LL_VECTOR_REGISTER_SIZE), "");

        bb->sseRegisters[i] = phiNode;
        bb->phiNodesSseRegisters[i] = phiNode;
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
    else if (endType != IT_RET && endType != IT_Invalid) // Any other instruction which is not a terminator
        LLVMBuildBr(state->builder, bb->nextFallThrough->llvmBB);
}

/**
 * Fill PHI nodes after the IR for all basic blocks of the function is
 * generated.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 **/
void
ll_basic_block_fill_phis(LLBasicBlock* bb)
{
    if (bb->predCount == 0)
        return;

    LLVMValueRef values[bb->predCount];
    LLVMBasicBlockRef bbs[bb->predCount];

    for (int j = 0; j < RI_GPMax; j++)
    {
        for (size_t i = 0; i < bb->predCount; i++)
        {
            bbs[i] = bb->preds[i]->llvmBB;
            values[i] = bb->preds[i]->gpRegisters[j];
        }

        LLVMAddIncoming(bb->phiNodesGpRegisters[j], values, bbs, bb->predCount);
    }

    for (int j = 0; j < RI_XMMMax; j++)
    {
        for (size_t i = 0; i < bb->predCount; i++)
        {
            bbs[i] = bb->preds[i]->llvmBB;
            values[i] = bb->preds[i]->sseRegisters[j];
        }

        LLVMAddIncoming(bb->phiNodesSseRegisters[j], values, bbs, bb->predCount);
    }

    for (int j = 0; j < RFLAG_Max; j++)
    {
        for (size_t i = 0; i < bb->predCount; i++)
        {
            bbs[i] = bb->preds[i]->llvmBB;
            values[i] = bb->preds[i]->flags[j];
        }

        LLVMAddIncoming(bb->phiNodesFlags[j], values, bbs, bb->predCount);
    }
}

/**
 * Get a pointer for a given register in the appropriate register file.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \param reg The register
 * \returns A pointer to the LLVM value in the register file.
 **/
static LLVMValueRef*
ll_basic_block_get_register_ptr(LLBasicBlock* bb, Reg reg)
{
    switch (reg.rt)
    {
        case RT_GP8:
        case RT_GP16:
        case RT_GP32:
        case RT_GP64:
            return &bb->gpRegisters[reg.ri];
        case RT_GP8Leg:
            if (reg.ri >= RI_AH && reg.ri < RI_R8L)
                return &bb->gpRegisters[reg.ri - RI_AH];
            return &bb->gpRegisters[reg.ri];
        case RT_XMM:
        case RT_YMM:
            return &bb->sseRegisters[reg.ri];
        case RT_IP:
            return &bb->ipRegister;
        case RT_Flag:
        case RT_X87:
        case RT_MMX:
        case RT_ZMM:
        case RT_Max:
        case RT_None:
        default:
            warn_if_reached();
    }

    return NULL;
}

/**
 * Get a register value of the basic block.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \param reg The register
 * \returns The current register value
 **/
LLVMValueRef
ll_basic_block_get_register(LLBasicBlock* bb, Reg reg)
{
    return *ll_basic_block_get_register_ptr(bb, reg);
}

/**
 * Set a register value of the basic block.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \param reg The register
 * \param value The new value
 **/
void
ll_basic_block_set_register(LLBasicBlock* bb, Reg reg, LLVMValueRef value)
{
    LLVMValueRef* registerPtr = ll_basic_block_get_register_ptr(bb, reg);
    *registerPtr = value;
}

/**
 * Get a flag value of the basic block.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \param flag The flag
 * \returns The current flag value
 **/
LLVMValueRef
ll_basic_block_get_flag(LLBasicBlock* bb, int flag)
{
    return bb->flags[flag];
}

/**
 * Set a flag value of the basic block.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \param flag The flag
 * \param value The new value
 **/
void
ll_basic_block_set_flag(LLBasicBlock* bb, int flag, LLVMValueRef value)
{
    bb->flags[flag] = value;
}

/**
 * Get the flag cache.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param bb The basic block
 * \returns The flag cache
 **/
LLFlagCache*
ll_basic_block_get_flag_cache(LLBasicBlock* bb)
{
    return &bb->flagCache;
}

/**
 * @}
 **/
