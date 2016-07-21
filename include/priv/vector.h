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

#include "common.h"

#include <stdint.h>

typedef struct _VRequest VRequest;
struct _VRequest
{
    uint64_t func;
    int pCount;
    int retElemSize;
    int par1ElemSize;
    int par2ElemSize;

    Rewriter* r;
};

// dispatcher
uint64_t handleVectorCall(uint64_t f, EmuState* es, bool verb);

// replacement functions

// for dbrew_apply4_R8V8
void SSE_apply4_R8V8(uint64_t f, double* ov, double* iv);
void AVX_apply4_R8V8(uint64_t f, double* ov, double* iv);

// for dbrew_apply4_R8V8V8
void SSE_apply4_R8V8V8(uint64_t f, double* ov, double* i1v, double* i2v);
void AVX_apply4_R8V8V8(uint64_t f, double* ov, double* i1v, double* i2v);
