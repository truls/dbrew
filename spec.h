/**
 * Simple x86_64 emulator/re-generator
 * (c) 2015, Josef Weidendorfer, GPLv2+
 */

#ifndef SPEC_H
#define SPEC_H

#include <stdint.h>

typedef void (*void_func)(void);

// piece of decoded x86_64 instructions
typedef struct _Code Code;

// allocate space for a given number of decoded instructions
Code* allocCode(int capacity, int capture_capacity);

// decode binary code starting at function pointer <f> into <c>
void decodeFunc(Code* c, uint64_t f, int max, int stopAtRet);

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
