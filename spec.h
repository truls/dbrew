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
 * - markInt(int, flags): returned value gets marked with meta info
 * - tagInt(int, char*):  returned value gets marked with tag
 *
 * Meta states (attached to values stored in registers/tracked memory)
 * - constant: rewrite code to specialize for the known value/condition
 * - unknown: "downgrade" a known value to be unknown for rewriting
 *            This forbids loop unrolling with known loop variable
 * - recursively known: for values same as known, dereferecing keeps state
 * - tracking pointer: request to track meta state of values dereferenced
 *            through this pointer by maintaining difference to base address.
 *            Example: on function entry, the stack pointer defaults to this
 *            meta state, using its current value as base. This allows to track
 *            the meta state for values on stack
 * - expected: when used next time, check for expected value and create
 *            new path with guard, setting value known. Multiple times allowed
 * - tracking value: maintain set of value tags this value depends on
 *
 * Configuration for calling observing hooks during rewriting:
 * - pass tagged values as parameters
 * - use return value as callback function to be inserted?
 *
 * Callback types:
 * - at memory read: address is parameter, address can be replaced
 * - at memory write: address/value is parameter, write may be suppressed
 * - at value usage: value, operations are parameters
 * - at operation: operand values are parameters
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
 *  void setFuncFlags(Rewriter* r, func_t f, int pNo, MetaState s)
 *
 *   Add meta state for parameter <pNo> of function <f> on entering.
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

// opaque data structures used in interface
typedef struct _Rewriter Rewriter;
typedef struct _DBB DBB;

// allocate space for a given number of decoded instructions
Rewriter* brew_new(void);

// free rewriter resources
void brew_free(Rewriter*);

// configure size of internal buffer space of a rewriter
void brew_set_decoding_capacity(Rewriter* r,
                                int instrCapacity, int bbCapacity);
void brew_set_capture_capacity(Rewriter* r,
                               int instrCapacity, int bbCapacity,
                               int codeCapacity);

// set function to rewrite
// this clears any previously decoded/captured instructions
void brew_set_function(Rewriter* rewriter, uint64_t f);

// set rewriter activities to be verbose or quiet
void brew_verbose(Rewriter* rewriter,
                  Bool decode, Bool emuState, Bool emuSteps);
void brew_optverbose(Rewriter* r, Bool v);

// decode a piece of x86 binary code starting add address <f>
DBB* brew_decode(Rewriter* c, uint64_t f);

// decode and print <count> instructions starting add address <f>
void brew_decode_print(Rewriter* c, uint64_t f, int count);

// print instructions from a decoded basic block
void brew_print_decoded(DBB* bb);

// initialize emulator, use given stack size
void brew_set_stacksize(Rewriter *c, int stacksize);

// emulate the given decoded binary code
// initialize state with function parameters ('...')
uint64_t brew_emulate_capture(Rewriter* r, ...);

// buffer with regenerated code, captured from emulation
uint64_t brew_generated_code(Rewriter* c);
int brew_generated_size(Rewriter* c);

// configure rewriter
void brew_config_reset(Rewriter* c);
void brew_config_staticpar(Rewriter* c, int staticParPos);
void brew_config_returnfp(Rewriter* c);
void brew_config_force_unknown(Rewriter* r, int depth);

// function which can be used in code to be rewritten:
// change capture state for value <v> to become dynamic/static
uint64_t makeDynamic(uint64_t v);
uint64_t makeStatic(uint64_t v);


// convenience functions, using default rewriter
void brew_def_verbose(Bool decode, Bool emuState, Bool emuSteps);
uint64_t brew_rewrite(uint64_t func, ...);


#endif // SPEC_H
