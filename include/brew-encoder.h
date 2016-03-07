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

#ifndef BREW_ENCODER
#define BREW_ENCODER

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <brew-common.h>

//---------------------------------------------------------------
// Functions to find/allocate new (captured) basic blocks (CBBs).
// A CBB is keyed by a function address and world state ID
// (actually an emulator state esID)

// remove any previously allocated CBBs (keep allocated memory space)
void resetCapturing(Rewriter* r);


// return 0 if not found
CBB *findCaptureBB(Rewriter* r, uint64_t f, int esID);


// allocate a BB structure to collect instructions for capturing
CBB* getCaptureBB(Rewriter* c, uint64_t f, int esID);


int pushCaptureBB(Rewriter* r, CBB* bb);


CBB* popCaptureBB(Rewriter* r);


Instr* newCapInstr(Rewriter* r);


// capture a new instruction
void capture(Rewriter* r, Instr* instr);

// generate code for a captured BB
void generate(Rewriter* c, CBB* cbb);

#endif
