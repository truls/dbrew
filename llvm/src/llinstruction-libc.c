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
#include <llflags-internal.h>
#include <llfunction.h>
#include <llfunction-internal.h>
#include <lloperand-internal.h>
#include <llsupport-internal.h>

/**
 * \defgroup LLInstructionGP General Purpose Instructions
 * \ingroup LLInstruction
 *
 * @{
 **/

void ll_instruction_memcpy(Instr* instr, LLState* state)
{
    Reg di = getReg(RT_GP64, RI_DI);
    Reg si = getReg(RT_GP64, RI_SI);
    Reg d = getReg(RT_GP64, RI_D);

    LLVMTypeRef ptrt = ll_register_facet_type(FACET_PTR, state);
    LLVMTypeRef i64t = ll_register_facet_type(FACET_I64, state);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(state->context);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(state->context);
    LLVMValueRef dest = ll_get_register(di, FACET_PTR, state);
    LLVMValueRef src = ll_get_register(si, FACET_PTR, state);
    //LLVMValueRef dest = ll_get_register(di, FACET_I64, state);
    //LLVMValueRef src = ll_get_register(si, FACET_I64, state);
    LLVMValueRef len = ll_get_register(d, FACET_I64, state);
    LLVMValueRef aligned = LLVMConstInt(i32, 0, false);
    LLVMValueRef vol = LLVMConstInt(i1, 0, false);

    LLVMValueRef args[5] = {dest, src, len, aligned, vol};
    LLVMTypeRef types[5] = {ptrt, ptrt, i64t, i32, i1};

    //printf("Got values %p %p %ul\n", (void* )LLVMConstIntGetSExtValue(dest),
    //       (void *) LLVMConstIntGetSExtValue(src),
    //       LLVMConstIntGetSExtValue(len));

    LLVMValueRef memcpyIntr = ll_support_get_intrinsic(state->module,
                                                       LL_INTRINSIC_MEMCPY,
                                                       types, 3);

    LLVMBuildCall(state->builder, memcpyIntr, args, 5, "");

}
