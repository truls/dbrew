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
uint64_t convertToVector(VRequest* v, bool verb)
{
    Rewriter* r = dbrew_new();
    if (verb) {
        dbrew_verbose(r, true, true, true);
        printf("Generating vectorized variant of %lx\n", v->func);
    }
    v->r = r;
    dbrew_set_function(r, v->func);
    // FIXME: specific for function taking a double, returing a double
    //        take config from <v>
    dbrew_config_returnfp(r);
    dbrew_config_parcount(r, v->pCount);
    return dbrew_rewrite(r, 0.0, 0.0);
}

// TODO: remember which variants already were generated
// e.g. in "nested rewriters" of current rewriter
uint64_t handleVectorCall(uint64_t f, EmuState* es, bool verb)
{
    VRequest vr;
    uint64_t rf; // redirect original vector API call to this variant

    if (f == (uint64_t)dbrew_apply4_R8V8) {
        vr.pCount = 1;
        vr.retElemSize  = 8;
        vr.par1ElemSize = 8;
#ifdef __AVX__
        rf = (uint64_t) AVX_apply4_R8V8;
#else
        rf = (uint64_t) SSE_apply4_R8V8;
#endif
    }
    else if (f == (uint64_t)dbrew_apply4_R8V8V8) {
        vr.pCount = 2;
        vr.retElemSize  = 8;
        vr.par1ElemSize = 8;
        vr.par2ElemSize = 8;
#ifdef __AVX__
        rf = (uint64_t) AVX_apply4_R8V8V8;
#else
        rf = (uint64_t) SSE_apply4_R8V8V8;
#endif
    }
    else
        assert(0);

    // re-direct from scalar to vectorized kernel (function pointer in par1)
    vr.func = es->reg[RI_DI];
    es->reg[RI_DI] = convertToVector(&vr, verb);

    return rf;
}
