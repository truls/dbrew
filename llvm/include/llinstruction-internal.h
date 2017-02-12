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

#ifndef LL_INSTRUCTION_H
#define LL_INSTRUCTION_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#include <instr.h>

#include <llcommon.h>
#include <llcommon-internal.h>

void ll_instruction_movgp(Instr*, LLState*);
void ll_instruction_add(Instr*, LLState*);
void ll_instruction_sub(Instr*, LLState*);
void ll_instruction_cmp(Instr*, LLState*);
void ll_instruction_test(Instr*, LLState*);
void ll_instruction_notneg(Instr*, LLState*);
void ll_instruction_incdec(Instr*, LLState*);
void ll_instruction_imul(Instr*, LLState*);
void ll_instruction_lea(Instr*, LLState*);
void ll_instruction_cmov(Instr*, LLState*);
void ll_instruction_setcc(Instr*, LLState*);
void ll_instruction_cdqe(Instr*, LLState*);

void ll_instruction_call(Instr*, LLState*);
void ll_instruction_ret(Instr*, LLState*);

void ll_instruction_stack(Instr*, LLState*);

void ll_instruction_movq(Instr* instr, LLState* state);
void ll_instruction_movs(Instr* instr, LLState* state);
void ll_instruction_movp(Instr* instr, LLState* state);
void ll_instruction_movlp(Instr* instr, LLState* state);
void ll_instruction_movhps(Instr* instr, LLState* state);
void ll_instruction_movhpd(Instr* instr, LLState* state);
void ll_instruction_unpckl(Instr* instr, LLState* state);

void ll_generate_instruction(Instr*, LLState*);

#endif
