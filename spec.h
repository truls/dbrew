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
typedef struct _Code Rewriter;
typedef struct _BB BB;

// allocate space for a given number of decoded instructions
Rewriter* allocRewriter();

// clear <c> from decoded/captured instructions
void setFunc(Rewriter* rewriter, uint64_t f);

// set Code activities verbose or quiet
void setVerbosity(Rewriter* rewriter, Bool decode, Bool emuState, Bool emuSteps);

// decode the basic block starting at f (automatically triggered by emulator)
BB* decodeBB(Rewriter* c, uint64_t f);

// print instructions in <c>
void printCode(Rewriter* c);

// initialize emulator, use given stack size
void configEmuState(Rewriter *c, int stacksize);

// initialize emulator, use stack from cc
void useSameStack(Rewriter* c, Rewriter* cc);

// emulate the given decoded binary code
// initialize state with function parameters ('...')
uint64_t rewrite(Rewriter* c, ...);

// buffer with regenerated code, captured from emulation
uint64_t generatedCode(Rewriter* c);
int generatedCodeSize(Rewriter* c);

// define a parameter(s) to assume static for emulation
void setRewriteConfig(Rewriter* c, int staticPos);
void setRewriteConfig2(Rewriter* c, int staticPos1, int staticPos2);

#endif // SPEC_H
