
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
