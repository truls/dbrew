/**
 * Simple x86_64 code rewriter (decoder/emulator/re-generator)
 * (c) 2015, Josef Weidendorfer, GPLv2+
 *
 * This library allows functions to be rewritten at runtime in specified
 * ways, that is, rewriting happens on the binary level. Rewritten functions
 * are called in exact the same way as the original function.
 *
 * Envisioned use cases:
 *
 * - partial evaluation/specialization at rewriting time.
 *   This allows to capture values of some variables during rewriting,
 *   such that each rewriting creates a special version of a generic function.
 *
 * - observe/insert/modify properties of binary code.
 *   - observe reads/write accesses to variables/data structures
 *   - insert function calls before/after operations
 *   - modify existing function calls/addresses of accesses
 *
 * To allow rewriting configurations to be specified on the binary level in
 * an architecture-independent way, we use the calling convention of the ABI.
 * That is, each configuration is bound to function pointers and properties of
 * parameters and return values of functions.
 *
 * Configuration for functions (providing function pointer):
 * - keep function call, allow inlining, or remove call completely?
 * - replace by provided wrapper function?
 * - add callback on read/write accesses to variables/memory?
 * - error out when detecting unknown control path?
 *
 * Configuration for function parameters / return value:
 * - value to be captured as being constant?
 *   (for pointers: all data accessable through pointer captured)
 * - constant value to be assumed as being variable?
 * - annotate value with name for later reference
 *
 * There are callbacks called at rewriting time (observers) and to be
 * called at execution time of the rewritten function (may be inlined).
 * All callbacks can be configured to receive named values as parameters
 * for context information. Function and memory address replacement works
 * by the callback returning the information to be replaced.
 *
 * In observers, rewritten code can be requested as byte sequence.
 * Any named values used in the code is given with offsets into the sequence.
 * This can be used as macro functionality for writing fast generators from
 * code templates, provided by the rewriter.
 *
 * Simple already defined functions are provided for convenience:
 * - markUnknownInt(int): returned value gets marked as unknown.
 *   This allows to forbid unrolling of loops using
 *       for(int i=markUnknownInt(0); i<10; i++) { .. }
 * - markNamedInt(int, char*): annotate returned value with name
 *
 *
 * Basic interface:
 *
 *  typedef ... (func_t*)(...)
 *
 *  Rewriter* allocRewriter();
 *
 *   Create a new rewriter instance
 *
 * void setRewriterFuncFlags(Rewriter* r, func_t f, int pNo, RewriterFlags f)
 *
 *   Add metainformation for parameter <pNo> of function <f> to recognize.
 *   <f> can be the function to rewrite, but also a function which is called
 *   during rewriting.
 *
 *   Flags:
 *    - KNOWN : Set
 *    - ...
 *
 *  func_t rewrite(Rewriter* r, func_t myfunc, <myfunc parameters>...)
 *
 *    Rewrite function <myfunc>.
 */

#ifndef SPEC_H
#define SPEC_H

#include <stdint.h>

typedef void (*void_func)(void);

typedef enum { False, True } Bool;

// piece of decoded x86_64 instructions
typedef struct _Rewriter Rewriter;
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
