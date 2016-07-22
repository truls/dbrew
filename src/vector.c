/**
 * This file is part of DBrew, the dynamic binary rewriting library.
 *
 * (c) 2016, Josef Weidendorfer <josef.weidendorfer@gmx.de>
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

#include "vector.h"

#include <assert.h>
#include <stdio.h>

#include "dbrew.h"

/* Support for DBrew vector API
 *
 * - generate vectorized variants of a given function
 * - substitute vector API code with own version
 */






// returns function pointer to rewritten, vectorized variant
static
uint64_t convertToVector(Rewriter* r, uint64_t func, VectorizeReq vreq)
{
    // already done before?
    Rewriter* rr;
    for(rr = r->next; rr != 0; rr = rr->next) {
        if ((rr->vreq == vreq) && (rr->func == func)) {
            assert(rr->generatedCodeAddr != 0);
            return rr->generatedCodeAddr;
        }
    }

    rr = dbrew_new();
    // add to related rewriter list of <r>
    rr->next = r->next;
    r->next = rr;

    if (r->showEmuSteps) {
        dbrew_verbose(rr, true, true, true);
        printf("Generating vectorized variant of %lx\n", func);
    }
    dbrew_set_function(rr, func);
    rr->vreq = vreq;

    bool hasVReturn = false;
    int pCount = 0;
    switch(vreq) {
    case VR_DoubleX2_RV:  pCount = 1; hasVReturn = true; break;
    case VR_DoubleX2_RVV: pCount = 2; hasVReturn = true; break;
    case VR_DoubleX4_RV:  pCount = 1; hasVReturn = true; break;
    case VR_DoubleX4_RVV: pCount = 2; hasVReturn = true; break;
    default: assert(0);
    }
    if (hasVReturn)
        dbrew_config_returnfp(rr);
    dbrew_config_parcount(rr, pCount);
    return dbrew_rewrite(rr, 0.0, 0.0);
}

uint64_t handleVectorCall(Rewriter* r, uint64_t f, EmuState* es)
{
    uint64_t rf; // redirect original vector API call to this variant
    VectorizeReq vr;

    if (f == (uint64_t)dbrew_apply4_R8V8) {
#ifdef __AVX__
        vr = VR_DoubleX4_RV;
        rf = (uint64_t) AVX_apply4_R8V8;
#else
        vr = VR_DoubleX2_RV;
        rf = (uint64_t) SSE_apply4_R8V8;
#endif
    }
    else if (f == (uint64_t)dbrew_apply4_R8V8V8) {
#ifdef __AVX__
        vr = VR_DoubleX4_RVV;
        rf = (uint64_t) AVX_apply4_R8V8V8;
#else
        vr = VR_DoubleX2_RVV;
        rf = (uint64_t) SSE_apply4_R8V8V8;
#endif
    }
    else
        assert(0);

    // re-direct from scalar to vectorized kernel (function pointer in par1)
    es->reg[RI_DI] = convertToVector(r, es->reg[RI_DI], vr);

    return rf;
}
