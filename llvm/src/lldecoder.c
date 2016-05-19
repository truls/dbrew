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

static LLBasicBlock*
ll_decode_basic_block(Rewriter* dbrewDecoder, uintptr_t address, LLState* state)
{
    for (size_t i = 0; i < state->currentFunction->bbCount; i++)
    {
        LLBasicBlock* otherBB = state->currentFunction->bbs[i];
        Instr* lastInstr = otherBB->instrs + otherBB->instrCount - 1;

        if (otherBB->address == address)
            return otherBB;
        else if (otherBB->address < address && lastInstr->addr >= address)
        {
            // The basic block has to be split here. We can only come here by
            // a JMP or Jcc given the case that the condition is true.
            // Example:
            //   0: push rax
            //   1: inc rax
            //   2: jmp 1b
            // Needs to be split into two basic blocks such that the blocks are
            // really basic and we can jump correctly.

            size_t splitIndex = 0;

            for (size_t j = 1; j < otherBB->instrCount; j++)
            {
                if (otherBB->instrs[j].addr == address)
                {
                    splitIndex = j;
                }
            }

            // If we didn't find the instruction, the jump is into the middle of
            // an instruction. We don't handle these cases as no compiler would
            // ever emit such code.
            if (splitIndex == 0)
                warn_if_reached();

            return ll_basic_block_split(otherBB, splitIndex, state);
        }
    }

    FunctionConfig fc = {
        .func = state->currentFunction->decl.address,
        .size = 0x1000,
        .name = "?",
        .next = NULL
    };
    DBB* dbb = dbrew_decode(dbrewDecoder, address);
    dbb->fc = &fc;
    printf("Decoded BB at ?+%lx:\n", address - state->currentFunction->decl.address);
    dbrew_print_decoded(dbb);

    LLBasicBlock* bb = ll_basic_block_new(address);
    bb->instrs = dbb->instr;
    bb->instrCount = dbb->count;

    ll_function_add_basic_block(state->currentFunction, bb);

    Instr* lastInstr = bb->instrs + bb->instrCount - 1;
    InstrType type = lastInstr->type;


    LLBasicBlock* endOfBB = bb;
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
    // So the question is: has bb been split up? if yes, to which basic block
    // does the original last instruction belong?
    if (bb->instrCount != (size_t) dbb->count)
    {
        // So we have been split up. Lets search.
        for (size_t i = 0; i < state->currentFunction->bbCount; i++)
        {
            LLBasicBlock* otherBB = state->currentFunction->bbs[i];

            if (lastInstr->addr == otherBB->instrs[otherBB->instrCount - 1].addr)
            {
                endOfBB = otherBB;
            }
        }
    }

    if (next != NULL)
    {
        ll_basic_block_add_predecessor(next, endOfBB);
        endOfBB->nextBranch = next;
    }

    if (fallThrough != NULL)
    {
        ll_basic_block_add_predecessor(fallThrough, endOfBB);
        endOfBB->nextFallThrough = fallThrough;
    }

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
