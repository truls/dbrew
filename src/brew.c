/**
 * Simple x86_64 emulator/re-generator
 * (c) 2015, Josef Weidendorfer, GPLv2+
 */

#include "spec.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <brew-code-storage.h>
#include <brew-common.h>
#include <brew-instruction.h>
#include <brew-printer.h>
#include <brew-decoder.h>
#include <brew-emulator.h>
#include <brew-encoder.h>





// forward declarations
void freeEmuState(Rewriter*);
void freeCaptureConfig(Rewriter*);

/*------------------------------------------------------------*/
/* x86_64 Analyzers
 */



void brew_decode_print(Rewriter* c, uint64_t f, int count)
{
    DBB* dbb;
    int decoded = 0;

    c->decBBCount = 0;
    while(decoded < count) {
        dbb = brew_decode(c, f + decoded);
        decoded += dbb->size;
    }
    printDecodedBBs(c);
}

Rewriter* brew_new()
{
    Rewriter* r;
    int i;

    r = (Rewriter*) malloc(sizeof(Rewriter));

    // allocation of other members on demand, capacities may be reset

    r->decInstrCount = 0;
    r->decInstrCapacity = 0;
    r->decInstr = 0;

    r->decBBCount = 0;
    r->decBBCapacity = 0;
    r->decBB = 0;

    r->capInstrCount = 0;
    r->capInstrCapacity = 0;
    r->capInstr = 0;

    r->capBBCount = 0;
    r->capBBCapacity = 0;
    r->capBB = 0;
    r->currentCapBB = 0;
    r->capStackTop = -1;
    r->genOrderCount = 0;

    r->savedStateCount = 0;
    for(i=0; i< SAVEDSTATE_MAX; i++)
        r->savedState[i] = 0;

    r->capCodeCapacity = 0;
    r->cs = 0;

    r->cc = 0;
    r->es = 0;

    // optimization passes
    r->addInliningHints = True;
    r->doCopyPass = True;

    // default: debug off
    r->showDecoding = False;
    r->showEmuState = False;
    r->showEmuSteps = False;

    return r;
}

void initRewriter(Rewriter* r)
{
    if (r->decInstr == 0) {
        // default
        if (r->decInstrCapacity == 0) r->decInstrCapacity = 500;
        r->decInstr = (Instr*) malloc(sizeof(Instr) * r->decInstrCapacity);
    }
    r->decInstrCount = 0;

    if (r->decBB == 0) {
        // default
        if (r->decBBCapacity == 0) r->decBBCapacity = 50;
        r->decBB = (DBB*) malloc(sizeof(DBB) * r->decBBCapacity);
    }
    r->decBBCount = 0;

    if (r->capInstr == 0) {
        // default
        if (r->capInstrCapacity == 0) r->capInstrCapacity = 500;
        r->capInstr = (Instr*) malloc(sizeof(Instr) * r->capInstrCapacity);
    }
    r->capInstrCount = 0;

    if (r->capBB == 0) {
        // default
        if (r->capBBCapacity == 0) r->capBBCapacity = 50;
        r->capBB = (CBB*) malloc(sizeof(CBB) * r->capBBCapacity);
    }
    r->capBBCount = 0;
    r->currentCapBB = 0;

    if (r->cs == 0) {
        if (r->capCodeCapacity == 0) r->capCodeCapacity = 3000;
        if (r->capCodeCapacity >0)
            r->cs = initCodeStorage(r->capCodeCapacity);
    }
    if (r->cs)
        r->cs->used = 0;
}

void brew_free(Rewriter* r)
{
    if (!r) return;

    free(r->decInstr);
    free(r->decBB);
    free(r->capInstr);
    free(r->capBB);

    freeCaptureConfig(r);
    freeEmuState(r);

    if (r->cs)
        freeCodeStorage(r->cs);
    free(r);
}

void brew_set_decoding_capacity(Rewriter* r,
                                 int instrCapacity, int bbCapacity)
{
    r->decInstrCapacity = instrCapacity;
    free(r->decInstr);
    r->decInstr = 0;

    r->decBBCapacity = bbCapacity;
    free(r->decBB);
    r->decBB = 0;
}

void brew_set_capture_capacity(Rewriter* r,
                                int instrCapacity, int bbCapacity,
                                int codeCapacity)
{
    r->capInstrCapacity = instrCapacity;
    free(r->capInstr);
    r->capInstr = 0;

    r->capBBCapacity = bbCapacity;
    free(r->capBB);
    r->capBB = 0;

    if (r->cs)
        freeCodeStorage(r->cs);
    r->cs = 0;
    r->capCodeCapacity = codeCapacity;
}


void brew_set_function(Rewriter* rewriter, uint64_t f)
{
    rewriter->func = f;

    // reset all decoding/state
    initRewriter(rewriter);
    brew_config_reset(rewriter);

    freeEmuState(rewriter);
}

void brew_verbose(Rewriter* rewriter,
                  Bool decode, Bool emuState, Bool emuSteps)
{
    rewriter->showDecoding = decode;
    rewriter->showEmuState = emuState;
    rewriter->showEmuSteps = emuSteps;
}

void brew_optverbose(Rewriter* r, Bool v)
{
    r->showOptSteps = v;
}

uint64_t brew_generated_code(Rewriter* c)
{
    if ((c->cs == 0) || (c->cs->used == 0))
        return 0;

    if (c->genOrderCount == 0) return 0;
    return c->genOrder[0]->addr2;

    //return (uint64_t) c->cs->buf;
}

int brew_generated_size(Rewriter* c)
{
    if ((c->cs == 0) || (c->cs->used == 0))
        return 0;

    if (c->genOrderCount == 0) return 0;
    return c->cs->used - (c->genOrder[0]->addr2 - (uint64_t) c->cs->buf);

    //return c->cs->used;
}

void freeCode(Rewriter* c)
{
    if (c->cs)
        freeCodeStorage(c->cs);

    free(c->cc);
    free(c->es);

    free(c->decBB);
    free(c->decInstr);
    free(c);
}


//-----------------------------------------------------------------
// convenience functions, using defaults

Rewriter* defaultRewriter = 0;

Rewriter* getDefaultRewriter()
{
    if (!defaultRewriter)
        defaultRewriter = brew_new();

    return defaultRewriter;
}

void brew_def_verbose(Bool decode, Bool emuState, Bool emuSteps)
{
    brew_verbose(getDefaultRewriter(), decode, emuState, emuSteps);
}

uint64_t brew_rewrite(uint64_t func, ...)
{
    Rewriter* r;
    va_list argptr;

    r = getDefaultRewriter();
    brew_set_function(r, func);

    va_start(argptr, func);
    // throw away result of emulation
    vEmulateAndCapture(r, argptr);
    va_end(argptr);

    return brew_generated_code(r);
}
