/**
 * Simple x86_64 emulator/re-generator
 * (c) 2015, Josef Weidendorfer, GPLv2+
 */

#ifndef SPEC_H
#define SPEC_H

#include <stdint.h>

typedef void (*void_func)(void);

typedef enum { False, True } Bool;

// piece of decoded x86_64 instructions
typedef struct _Code Code;
typedef struct _BB BB;

// allocate space for a given number of decoded instructions
Code* allocCode(int instr_capacity, int bb_capacity, int capture_capacity);

// clear <c> from decoded/captured instructions
void setFunc(Code* c, uint64_t f);

// set Code activities verbose or quiet
void setCodeVerbosity(Code* c, Bool decode, Bool emuState, Bool emuSteps);

// decode the basic block starting at f (automatically triggered by emulator)
BB* decodeBB(Code* c, uint64_t f);

// print instructions in <c>
void printCode(Code* c);

// initialize emulator, use given stack size
void configEmuState(Code *c, int stacksize);

// initialize emulator, use stack from cc
void useSameStack(Code* c, Code* cc);

// emulate the given decoded binary code
// initialize state with function parameters ('...')
uint64_t emulate(Code* c, ...);

// buffer with regenerated code, captured from emulation
uint64_t capturedCode(Code* c);
int capturedCodeSize(Code* c);

// define a parameter(s) to assume static for emulation
void setCaptureConfig(Code* c, int constPos);
void setCaptureConfig2(Code* c, int constPos1, int constPos2);

#endif // SPEC_H
