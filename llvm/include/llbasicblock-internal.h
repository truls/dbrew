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

#ifndef LL_BASIC_BLOCK_H
#define LL_BASIC_BLOCK_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdlib.h>
#include <llvm-c/Core.h>

#include <instr.h>

#include <llcommon.h>
#include <llcommon-internal.h>


enum {
    RFLAG_ZF = Reg_AX + 48 + 0,
    RFLAG_SF = Reg_AX + 48 + 1,
    RFLAG_PF = Reg_AX + 48 + 2,
    RFLAG_CF = Reg_AX + 48 + 3,
    RFLAG_OF = Reg_AX + 48 + 4,
    RFLAG_AF = Reg_AX + 48 + 5,
};

struct LLFlagCache {
    bool valid;
    LLVMValueRef operand1;
    LLVMValueRef operand2;
    LLVMValueRef result;
};

typedef struct LLFlagCache LLFlagCache;

struct LLBasicBlock {
    uintptr_t address;

    size_t instrCount;
    Instr* instrs;

    LLBasicBlock* nextBranch;
    LLBasicBlock* nextFallThrough;

    // Predecessors needed for phi nodes
    size_t predCount;
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
     * \brief The LLVM values of the architectural general purpose registers
     *
     * Ordering: 16 GP regs (i64), IP reg (i64), 32 AVX regs (i256), 6 flags.
     * The registers always store integers of an appropriate length.
     **/
    LLVMValueRef registers[55];

    /**
     * \brief The phi nodes for the registers
     **/
    LLVMValueRef phiNodes[55];

    LLFlagCache flagCache;
};

LLBasicBlock* ll_basic_block_new(uintptr_t);
void ll_basic_block_declare(LLBasicBlock*, LLState*);
void ll_basic_block_add_predecessor(LLBasicBlock*, LLBasicBlock*);
LLBasicBlock* ll_basic_block_split(LLBasicBlock*, size_t, LLState*);
void ll_basic_block_build_ir(LLBasicBlock*, LLState*);
void ll_basic_block_fill_phis(LLBasicBlock*);

#endif
