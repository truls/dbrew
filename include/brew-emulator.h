
#ifndef BREW_EMULATOR
#define BREW_EMULATOR

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#include <brew-common.h>
#include <brew-instruction.h>


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


void brew_config_reset(Rewriter* c);


CaptureConfig* getCaptureConfig(Rewriter* c);

void brew_config_staticpar(Rewriter* c, int staticParPos);

/**
 * This allows to specify for a given function inlining depth that
 * values produced by binary operations always should be forced to unknown.
 * Thus, when result is known, it is converted to unknown state with
 * the value being loaded as immediate into destination.
 *
 * Brute force approach to prohibit loop unrolling.
 */
void brew_config_force_unknown(Rewriter* r, int depth);

void brew_config_returnfp(Rewriter* r);

void brew_config_branches_known(Rewriter* r, Bool b);



//---------------------------------------------------------
// emulator functions


//----------------------------------------------------------
// Emulator for instruction types



// return 0 to fall through to next instruction, or return address to jump to
uint64_t emulateInstr(Rewriter* c, EmuState* es, Instr* instr);



//----------------------------------------------------------
// Rewrite engine

// FIXME: this always assumes 5 parameters
uint64_t vEmulateAndCapture(Rewriter* c, va_list args);

uint64_t brew_emulate_capture(Rewriter* r, ...);

#endif
