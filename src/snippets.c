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


#include "dbrew.h"
#include "vector.h"

#include <immintrin.h>

/*
 * Functions provided by DBrew for users to call
 *
 * The semantics of these functions are clearly defined and known
 * to DBrew. This way, they can be used to provide custom meta data
 * to help rewriting, and DBrew can arbitrarly replace their
 * instructions, e.g. make a NOP instruction out of it or substitue
 * with other variants.
 */


/* Vector API:
 * The rewriter will try to generated vectorized variants from a given
 * function. Further, it will replace the functions of the vector API
 * with generated code calling the vectorized variants.
 */

// 4x call f (signature double => double) and map to input/output vector iv/ov
__attribute__ ((noinline))
void dbrew_apply4_R8V8(dbrew_func_R8V8_t f, double* ov, double* iv)
{
    ov[0] = (f)(iv[0]);
    ov[1] = (f)(iv[1]);
    ov[2] = (f)(iv[2]);
    ov[3] = (f)(iv[3]);
}

// 4x call f (signature double,double => double) and map to vectors i1v,i2v,ov
__attribute__ ((noinline))
void dbrew_apply4_R8V8V8(dbrew_func_R8V8V8_t f,
                         double* ov, double* i1v, double* i2v)
{
    ov[0] = (f)(i1v[0], i2v[0]);
    ov[1] = (f)(i1v[1], i2v[1]);
    ov[2] = (f)(i1v[2], i2v[2]);
    ov[3] = (f)(i1v[3], i2v[3]);
}


//
// replacement functions
//

// for dbrew_apply4_R8V8

typedef __m128d (*dbrew_func_R8V8_SSE_t)(__m128d);
void SSE_apply4_R8V8(uint64_t f, double* ov, double* iv)
{
    // ov[0] = (f)(iv[0]);
    // ov[1] = (f)(iv[1]);
    // ov[2] = (f)(iv[2]);
    // ov[3] = (f)(iv[3]);
    dbrew_func_R8V8_SSE_t vf = (dbrew_func_R8V8_SSE_t) f;
    ((__m128d*)ov)[0] = (vf)( ((__m128d*)iv)[0] );
    ((__m128d*)ov)[1] = (vf)( ((__m128d*)iv)[1] );
}

#ifdef __AVX__
typedef __m256d (*dbrew_func_R8V8_AVX_t)(__m256d);
void AVX_apply4_R8V8(uint64_t f, double* ov, double* iv)
{
    dbrew_func_R8V8_AVX_t vf = (dbrew_func_R8V8_AVX_t) f;
    ((__m256d*)ov)[0] = (vf)( ((__m256d*)iv)[0] );
}
#endif


// for dbrew_apply4_R8V8

typedef __m128d (*dbrew_func_R8V8V8_SSE_t)(__m128d,__m128d);

void SSE_apply4_R8V8V8(uint64_t f, double* ov, double* i1v, double* i2v)
{
    // ov[0] = (f)(i1v[0], i2v[0]);
    // ov[1] = (f)(i1v[1], i2v[1]);
    // ov[2] = (f)(i1v[2], i2v[2]);
    // ov[3] = (f)(i1v[3], i2v[3]);
    dbrew_func_R8V8V8_SSE_t vf = (dbrew_func_R8V8V8_SSE_t) f;
    ((__m128d*)ov)[0] = (vf)( ((__m128d*)i1v)[0], ((__m128d*)i2v)[0] );
    ((__m128d*)ov)[1] = (vf)( ((__m128d*)i1v)[1], ((__m128d*)i2v)[1] );
}

#ifdef __AVX__
typedef __m256d (*dbrew_func_R8V8V8_AVX_t)(__m256d,__m256d);
void AVX_apply4_R8V8V8(uint64_t f, double* ov, double* i1v, double* i2v)
{
    // ov[0] = (f)(i1v[0], i2v[0]);
    // ov[1] = (f)(i1v[1], i2v[1]);
    // ov[2] = (f)(i1v[2], i2v[2]);
    // ov[3] = (f)(i1v[3], i2v[3]);
    dbrew_func_R8V8V8_AVX_t vf = (dbrew_func_R8V8V8_AVX_t) f;
    ((__m256d*)ov)[0] = (vf)( ((__m256d*)i1v)[0], ((__m256d*)i2v)[0] );
}
#endif
