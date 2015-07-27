/**
 * Simple x86_64 emulator
 * (c) Josef Weidendorfer, GPLv2+
 *
 */

#ifndef SPEC_H
#define SPEC_H

#include <stdint.h>

typedef void (*void_func)(void);

// piece of decoded x86_64 instructions
typedef struct _Code Code;

// allocate space for a given number of decoded instructions
Code* allocCode(int capacity);

// decode binary code starting at function pointer <f> into <c>
void decodeFunc(Code* c, void_func f, int max, int stopAtRet);

// print instructions in <c>
void printCode(Code* c);

// initialize emulator, use given stack size
void initEmulatorState(int stacksize);

// emulate the given decoded binary code
// initialize state with function parameters ('...')
uint64_t emulate(Code* c, ...);

// specialize function for constant parameter 2
void_func spec2(void_func, ...);

#endif // SPEC_H
