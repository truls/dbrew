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

#ifndef EMULATE_H
#define EMULATE_H

#include <stdint.h>

#include "common.h"
#include "instr.h"


/*------------------------------------------------------------*/
/* x86_64 capturing emulator
 * trace execution in the emulator to capture code to generate
 *
 * We maintain states (known/static vs unknown/dynamic at capture time)
 * for registers and values on stack. To be able to do the latter, we
 * assume that the known values on stack do not get changed by
 * memory writes with dynamic address. This assumption should be fine,
 * as such behavior is dangerous and potentially a bug.
 *
 * At branches to multiple possible targets, we need to travers each path by
 * saving emulator state. After emulating one path, we roll back and
 * go the other path. As this may happen recursively, we do a kind of
 * back-tracking, with emulator states stored as stacks.
 * To allow for fast saving/restoring of emulator states, each part of
 * the emulation state (registers, bytes on stack) is given by a
 * EmuStateEntry (linked) list with the current value/state in front.
 * Saving copies the complete EmuState, inheriting the individual states.
 */

// exported functions

// get a new emulator state with stack size <size>
EmuState* allocEmuState(int size);
void freeEmuState(Rewriter* r);
void resetEmuState(EmuState* es);
// save current emulator state for later rollback, return ID
int saveEmuState(Rewriter* r);
// set current emulator state to previously saved state <esID>
void restoreEmuState(Rewriter* r, int esID);
void printEmuState(EmuState* es);
void printStaticEmuState(EmuState* es, int esID);

void resetCapturing(Rewriter* r);
CBB* getCaptureBB(Rewriter* c, uint64_t f, int esID);
int pushCaptureBB(Rewriter* r, CBB* bb);
CBB* popCaptureBB(Rewriter* r);
Instr* newCapInstr(Rewriter* r);
void capture(Rewriter* r, Instr* instr);
void captureRet(Rewriter* c, Instr* orig, EmuState* es);

// emulate <instr> by changing <es> and capture it if not static.
// return 0 to fall through to next instruction, or return address to jump to
uint64_t emulateInstr(Rewriter* c, EmuState* es, Instr* instr);

#endif // EMULATE_H
