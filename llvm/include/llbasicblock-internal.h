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

#include <common.h>
#include <instr.h>

#include <llcommon.h>
#include <llcommon-internal.h>


/**
 * \ingroup LLBasicBlock
 **/
enum {
    /**
     * \brief The zero flag
     **/
    RFLAG_ZF = 0,
    /**
     * \brief The sign flag
     **/
    RFLAG_SF,
    /**
     * \brief The parity flag
     **/
    RFLAG_PF,
    /**
     * \brief The carry flag
     **/
    RFLAG_CF,
    /**
     * \brief The overflow flag
     **/
    RFLAG_OF,
    /**
     * \brief The auxiliary carry flag
     **/
    RFLAG_AF,
    RFLAG_Max
};

/**
 * \ingroup LLBasicBlock
 * \brief Flag cache storing additional information about the flag register
 **/
struct LLFlagCache {
    /**
     * \brief Whether the information is valid
     **/
    bool valid;
    /**
     * \brief The first operand of the subtraction
     **/
    LLVMValueRef operand1;
    /**
     * \brief The second operand of the subtraction
     **/
    LLVMValueRef operand2;
    /**
     * \brief The result of the subtraction
     **/
    LLVMValueRef result;
};

typedef struct LLFlagCache LLFlagCache;

enum RegisterFacet {
    FACET_PTR,
    FACET_I8,
    FACET_I8H,
    FACET_I16,
    FACET_I32,
    FACET_I64,
    FACET_I128,
    FACET_I256,
    FACET_F32,
    FACET_F64,

    FACET_V2F32,

    FACET_V16I8,
    FACET_V8I16,
    FACET_V4I32,
    FACET_V2I64,
    FACET_V4F32,
    FACET_V2F64,
    FACET_COUNT,

    FACET_V32I8,
    FACET_V16I16,
    FACET_V8I32,
    FACET_V4I64,
    FACET_V8F32,
    FACET_V4F64,

};

#if LL_VECTOR_REGISTER_SIZE == 128
#define FACET_IVEC FACET_I128
#define FACET_VI8 FACET_V16I8
#define FACET_VI16 FACET_V8I16
#define FACET_VI32 FACET_V4I32
#define FACET_VI64 FACET_V2I64
#define FACET_VF32 FACET_V4F32
#define FACET_VF64 FACET_V2F64
#elif LL_VECTOR_REGISTER_SIZE == 256
#define FACET_IVEC FACET_I256
#define FACET_VI8 FACET_V32I8
#define FACET_VI16 FACET_V16I16
#define FACET_VI32 FACET_V8I32
#define FACET_VI64 FACET_V4I64
#define FACET_VF32 FACET_V8F32
#define FACET_VF64 FACET_V4F64
#error
#endif

typedef enum RegisterFacet RegisterFacet;

LLBasicBlock* ll_basic_block_new(uintptr_t);
LLBasicBlock* ll_basic_block_new_from_dbb(DBB*);
LLBasicBlock* ll_basic_block_new_from_cbb(CBB*);
void ll_basic_block_dispose(LLBasicBlock*);
void ll_basic_block_declare(LLBasicBlock*, LLState*);
void ll_basic_block_add_predecessor(LLBasicBlock*, LLBasicBlock*);
void ll_basic_block_truncate(LLBasicBlock*, size_t);
LLBasicBlock* ll_basic_block_split(LLBasicBlock*, size_t, LLState*);
void ll_basic_block_build_ir(LLBasicBlock*, LLState*);
void ll_basic_block_fill_phis(LLBasicBlock*, LLState*);

#define ll_get_register(reg,facet,state) ll_basic_block_get_register(state->currentBB,facet,reg,state)
#define ll_clear_register(reg,state) ll_basic_block_clear_register(state->currentBB,reg,state)
#define ll_set_register(reg,facet,value,clear,state) ll_basic_block_set_register(state->currentBB,facet,reg,value,clear,state)
#define ll_get_flag(reg,state) ll_basic_block_get_flag(state->currentBB,reg)
#define ll_set_flag(reg,value,state) ll_basic_block_set_flag(state->currentBB,reg,value)
#define ll_get_flag_cache(state) ll_basic_block_get_flag_cache(state->currentBB)

LLVMValueRef ll_basic_block_get_register(LLBasicBlock*, RegisterFacet, Reg, LLState*);
void ll_basic_block_clear_register(LLBasicBlock*, Reg, LLState*);
void ll_basic_block_set_register(LLBasicBlock*, RegisterFacet, Reg, LLVMValueRef, bool, LLState*);
LLVMValueRef ll_basic_block_get_flag(LLBasicBlock*, int);
void ll_basic_block_set_flag(LLBasicBlock*, int, LLVMValueRef);
LLFlagCache* ll_basic_block_get_flag_cache(LLBasicBlock*);

long ll_basic_block_find_address(LLBasicBlock*, uintptr_t);
void ll_basic_block_add_branches(LLBasicBlock*, LLBasicBlock*, LLBasicBlock*);
LLVMBasicBlockRef ll_basic_block_llvm(LLBasicBlock*);

#endif
