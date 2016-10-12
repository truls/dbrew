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

#ifndef LL_COMMON_H
#define LL_COMMON_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>

#include <llcommon.h>
#include <llfunction.h>


/**
 * \brief Emit a warning and jump into a debugger
 **/
#define warn_if_reached() do { printf("!WARN %s: Code should not be reached.\n", __PRETTY_FUNCTION__); __asm__("int3"); } while (0)

/**
 * \brief The size of a vector
 **/
#define LL_VECTOR_REGISTER_SIZE 128

struct LLBasicBlock;

typedef struct LLBasicBlock LLBasicBlock;

/**
 * \brief The LLVM state of the back-end.
 **/
struct LLState {
    /**
     * \brief The LLVM Context
     **/
    LLVMContextRef context;
    /**
     * \brief The LLVM Module
     **/
    LLVMModuleRef module;
    /**
     * \brief The LLVM Builder
     **/
    LLVMBuilderRef builder;
    /**
     * \brief The LLVM execution engine
     **/
    LLVMExecutionEngineRef engine;

    /**
     * \brief The function count
     **/
    size_t functionCount;
    /**
     * \brief The allocated size for function
     **/
    size_t functionsAllocated;
    /**
     * \brief The functions of the module
     **/
    LLFunction** functions;

    /**
     * \brief The empty metadata node
     **/
    LLVMValueRef emptyMD;

    /**
     * \brief The loop unrolling metadata
     **/
    LLVMValueRef unrollMD;

    /**
     * \brief The current function
     **/
    LLFunction* currentFunction;
    /**
     * \brief The current basic block
     **/
    LLBasicBlock* currentBB;

    /**
     * \brief The global offset base
     **/
    uintptr_t globalOffsetBase;
    /**
     * \brief The global variable used to access constant memory regions. Points
     * to globalOffsetBase.
     **/
    LLVMValueRef globalBase;

    /**
     * \brief Whether unsafe pointer optimizations are enabled.
     **/
    bool enableUnsafePointerOptimizations;
    /**
     * \brief Whether overflow intrinsics should be used.
     **/
    bool enableOverflowIntrinsics;
    /**
     * \brief Whether unsafe floating-point optimizations may be applied.
     * Corresponds to -ffast-math.
     **/
    bool enableFastMath;
    /**
     * \brief Whether to force full loop unrolling on all loops
     **/
    bool enableFullLoopUnroll;
};

#endif
