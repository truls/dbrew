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
#include <llfunction-internal.h>

/**
 * \ingroup LLDecoder
 * \defgroup LLDecoder Decoder
 *
 * @{
 **/

/**
 * We want to ensure that each instruction belongs *to exactly one basic block*.
 * This is contrary to the handling of DBrew (which doesn't care). We care about
 * this because LLVM does not recognize two BB parts to be identical and
 * generates the code twice (which is not what we want).
 *
 *    Example:
 *      0: push rax
 *      1: inc rax
 *      2: jmp 1b
 **/
static LLBasicBlock*
ll_decode_basic_block(Rewriter* dbrewDecoder, uintptr_t address, LLState* state)
{
    for (size_t i = 0; i < state->currentFunction->bbCount; i++)
    {
        LLBasicBlock* otherBB = state->currentFunction->bbs[i];
        long index = ll_basic_block_find_address(otherBB, address);

        if (index == 0)
            return otherBB;
        else if (index > 0)
            // Needs to be split into two basic blocks such that the blocks are
            // really basic and we can jump correctly.
            return ll_basic_block_split(otherBB, index, state);
        // If index == -1, the basic block does not contain this address.
    }

    FunctionConfig fc = {
        .func = state->currentFunction->decl.address,
        .size = 0x1000,
        .name = (char*) "?",
        .next = NULL
    };
    DBB* dbb = dbrew_decode(dbrewDecoder, address);
    dbb->fc = &fc;

    printf("Decoded BB at ?+%lx:\n", address - state->currentFunction->decl.address);
    dbrew_print_decoded(dbb);

    LLBasicBlock* bb = ll_basic_block_new_from_dbb(dbb);
    ll_function_add_basic_block(state->currentFunction, bb);

    Instr* lastInstr = dbb->instr + dbb->count - 1;
    InstrType type = lastInstr->type;

    // In case the last instruction is already part of another BB.
    for (size_t i = 0; i < state->currentFunction->bbCount; i++)
    {
        LLBasicBlock* otherBB = state->currentFunction->bbs[i];
        long index = ll_basic_block_find_address(otherBB, lastInstr->addr);

        if (otherBB != bb && index >= 0)
        {
            ll_basic_block_truncate(bb, index);
            ll_basic_block_add_branches(bb, NULL, otherBB);

            return bb;
        }
    }


    LLBasicBlock* endOfBB = NULL;
    LLBasicBlock* next = NULL;
    LLBasicBlock* fallThrough = NULL;

    if (instrIsJcc(type) || type == IT_CALL)
    {
        fallThrough = ll_decode_basic_block(dbrewDecoder, lastInstr->addr + lastInstr->len, state);
    }

    if (type == IT_JMP || instrIsJcc(type))
    {
        next = ll_decode_basic_block(dbrewDecoder, lastInstr->dst.val, state);
    }

    // It may happen that bb has been split in the meantime.
    for (size_t i = 0; i < state->currentFunction->bbCount; i++)
    {
        LLBasicBlock* otherBB = state->currentFunction->bbs[i];

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

LLFunction*
ll_decode_function(Rewriter* dbrewDecoder, uintptr_t address, LLConfig* config, LLState* state)
{
    LLFunctionDecl decl = {
        .name = config->name,
        .address = address
    };

    LLFunction* function = ll_function_new(&decl, config, state);

    state->currentFunction = function;
    ll_decode_basic_block(dbrewDecoder, address, state);

    return function;
}

/**
 * @}
 **/
