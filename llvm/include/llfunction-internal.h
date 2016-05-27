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

#ifndef LL_FUNCTION_INTERNAL_H
#define LL_FUNCTION_INTERNAL_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <llvm-c/Core.h>

#include <llfunction.h>

#include <llcommon.h>
#include <llcommon-internal.h>


struct LLFunctionDecl {
    /**
     * \brief The name of the function
     **/
    const char* name;

    /**
     * \brief Address of the function
     **/
    uintptr_t address;

    // FunctionSignature sign;
    size_t noaliasParams; // For now, until we have a proper signature.

    /**
     * \brief The LLVM function value
     *
     * This is actually an LLVMValueRef and is plugged in automatically
     **/
    void* llvmFunction;
};

struct LLFunction {
    /**
     * \brief The function declaration
     **/
    LLFunctionDecl decl;

    /**
     * \brief The size of the stack in bytes
     **/
    size_t stackSize;

    /**
     * \brief The basic block count
     **/
    size_t bbCount;
    /**
     * \brief The allocated size for basic blocks
     **/
    size_t bbsAllocated;
    /**
     * \brief Array of basics blocks belonging to this function
     **/
    LLBasicBlock** bbs;

    /**
     * \brief The initial basic block, which is the entry point
     **/
    LLBasicBlock* initialBB;
};

LLFunction* ll_function_new(LLFunctionDecl*, LLConfig*, LLState*);
void ll_function_add_basic_block(LLFunction*, LLBasicBlock*);

#endif
