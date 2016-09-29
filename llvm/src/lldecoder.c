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

#include <common.h>
#include <instr.h>
#include <printer.h>

#include <lldecoder.h>

#include <llbasicblock-internal.h>
#include <llcommon.h>
#include <llcommon-internal.h>
#include <llfunction.h>
#include <llfunction-internal.h>

/**
 * \ingroup LLDecoder
 * \defgroup LLDecoder Decoder
 * \brief Decode assembly functions
 *
 * @{
 **/

/**
 * Decode a basic block at the given address. This function calls itself
 * recursively.
 *
 * This naive variant does not perform instruction de-duplication and exists
 * solely for testing purposes.
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param dbrewDecoder The decoder
 * \param address The address of the basic block
 * \param state The module state
 * \returns The decoded basic block, which is ready-to-use
 **/
static LLBasicBlock*
ll_decode_basic_block_naive(Rewriter* dbrewDecoder, uintptr_t address, LLState* state)
{
    for (size_t i = 0; i < state->currentFunction->u.definition.bbCount; i++)
    {
        LLBasicBlock* otherBB = state->currentFunction->u.definition.bbs[i];
        long index = ll_basic_block_find_address(otherBB, address);

        if (index == 0)
            return otherBB;
    }

    DBB* dbb = dbrew_decode(dbrewDecoder, address);

    LLBasicBlock* bb = ll_basic_block_new_from_dbb(dbb);
    ll_function_add_basic_block(state->currentFunction, bb);

    Instr* lastInstr = dbb->instr + dbb->count - 1;
    InstrType type = lastInstr->type;

    LLBasicBlock* next = NULL;
    LLBasicBlock* fallThrough = NULL;

    if (instrIsJcc(type) || type == IT_CALL)
        fallThrough = ll_decode_basic_block_naive(dbrewDecoder, lastInstr->addr + lastInstr->len, state);

    if (type == IT_JMP || instrIsJcc(type))
        next = ll_decode_basic_block_naive(dbrewDecoder, lastInstr->dst.val, state);

    ll_basic_block_add_branches(bb, next, fallThrough);

    return bb;
}

/**
 * Decode a basic block at the given address. This function calls itself
 * recursively.
 *
 * We want to ensure that each instruction belongs *to exactly one basic block*.
 * This is contrary to the handling of DBrew (which doesn't care). We care about
 * this because LLVM does not recognize two BB parts to be identical and
 * generates the code twice (which is not what we want).
 *
 * Here is an example code:
 *
 *     1: jmp 3f
 *     2: dec rax
 *     3: test rax, rax
 *     4: jnz 2b
 *     5: ret
 *
 * We want this code to be split up in four basic blocks as follows:
 *
 *     BB 1 jmp 3f
 *          branch to BB 3
 *     BB 2 dec rax
 *          fall-through to BB 3
 *     BB 3 test rax, rax
 *     BB 3 jnz 2b
 *          conditional branch to BB 2, or fall-through to BB 4
 *     BB 4 ret
 *
 * \private
 *
 * \author Alexis Engelke
 *
 * \param dbrewDecoder The decoder
 * \param address The address of the basic block
 * \param state The module state
 * \returns The decoded basic block, which is ready-to-use
 **/
static LLBasicBlock*
ll_decode_basic_block_dedup(Rewriter* dbrewDecoder, uintptr_t address, LLState* state)
{
    for (size_t i = 0; i < state->currentFunction->u.definition.bbCount; i++)
    {
        LLBasicBlock* otherBB = state->currentFunction->u.definition.bbs[i];
        long index = ll_basic_block_find_address(otherBB, address);

        if (index == 0)
            return otherBB;
        else if (index > 0)
            // Needs to be split into two basic blocks such that the blocks are
            // really basic and we can jump correctly.
            return ll_basic_block_split(otherBB, index, state);
        // If index == -1, the basic block does not contain this address.
    }

    DBB* dbb = dbrew_decode(dbrewDecoder, address);

    LLBasicBlock* bb = ll_basic_block_new_from_dbb(dbb);
    ll_function_add_basic_block(state->currentFunction, bb);

    Instr* lastInstr = dbb->instr + dbb->count - 1;
    InstrType type = lastInstr->type;

    // In case the last instruction is already part of another BB.
    for (size_t i = 0; i < state->currentFunction->u.definition.bbCount; i++)
    {
        LLBasicBlock* otherBB = state->currentFunction->u.definition.bbs[i];
        long index = ll_basic_block_find_address(otherBB, lastInstr->addr);

        if (otherBB != bb && index >= 0)
        {
            ll_basic_block_truncate(bb, index + 1);
            ll_basic_block_add_branches(bb, NULL, otherBB);

            return bb;
        }
    }


    LLBasicBlock* endOfBB = NULL;
    LLBasicBlock* next = NULL;
    LLBasicBlock* fallThrough = NULL;

    if (instrIsJcc(type) || type == IT_CALL)
        fallThrough = ll_decode_basic_block_dedup(dbrewDecoder, lastInstr->addr + lastInstr->len, state);

    if (type == IT_JMP || instrIsJcc(type))
        next = ll_decode_basic_block_dedup(dbrewDecoder, lastInstr->dst.val, state);

    // It may happen that bb has been split in the meantime.
    for (size_t i = 0; i < state->currentFunction->u.definition.bbCount; i++)
    {
        LLBasicBlock* otherBB = state->currentFunction->u.definition.bbs[i];

        if (ll_basic_block_find_address(otherBB, lastInstr->addr) >= 0)
        {
            if (endOfBB != NULL)
                warn_if_reached();

            endOfBB = otherBB;
        }
    }

    if (endOfBB == NULL)
        warn_if_reached();

    ll_basic_block_add_branches(endOfBB, next, fallThrough);

    return bb;
}

/**
 * Decode a function at the given address.
 *
 * \author Alexis Engelke
 *
 * \param dbrewDecoder The decoder
 * \param address The address of the function
 * \param config The function configuration, see #ll_function_new_definition
 * \param state The module state
 * \returns The decoded function
 **/
LLFunction*
ll_decode_function(Rewriter* dbrewDecoder, uintptr_t address, LLConfig* config, LLState* state)
{
    LLFunction* function = ll_function_new_definition(address, config, state);

    state->currentFunction = function;

    if (config->disableInstrDedup)
        ll_decode_basic_block_naive(dbrewDecoder, address, state);
    else
        ll_decode_basic_block_dedup(dbrewDecoder, address, state);

    if (ll_function_build_ir(function, state))
    {
        ll_function_dispose(function);
        return NULL;
    }

    return function;
}

/**
 * @}
 **/
