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

#ifndef BREW_DECODER
#define BREW_DECODER

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <brew-common.h>

Instr* nextInstr(Rewriter* c, uint64_t a, int len);
Instr* addSimple(Rewriter* c, uint64_t a, uint64_t a2, InstrType it);
Instr* addSimpleVType(Rewriter* c, uint64_t a, uint64_t a2, InstrType it, ValType vt);
Instr* addUnaryOp(Rewriter* c, uint64_t a, uint64_t a2,
                  InstrType it, Operand* o);

Instr* addBinaryOp(Rewriter* c, uint64_t a, uint64_t a2,
                   InstrType it, ValType vt, Operand* o1, Operand* o2);

Instr* addTernaryOp(Rewriter* c, uint64_t a, uint64_t a2,
                    InstrType it, Operand* o1, Operand* o2, Operand* o3);

// Parse RM encoding (r/m,r: op1 is reg or memory operand, op2 is reg/digit)
// Encoding see SDM 2.1
// Input: REX prefix, SegOverride prefix, o1 or o2 may be vector registers
// Fills o1/o2/digit and returns number of bytes parsed
int parseModRM(uint8_t* p,
               int rex, OpSegOverride o1Seg, Bool o1IsVec, Bool o2IsVec,
               Operand* o1, Operand* o2, int* digit);
// decode the basic block starting at f (automatically triggered by emulator)
DBB* brew_decode(Rewriter* c, uint64_t f);

#endif
