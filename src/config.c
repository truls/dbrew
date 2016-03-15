/**
 * This file is part of DBrew, the dynamic binary rewriting library.
 *
 * (c) 2015-2016, Josef Weidendorfer <josef.weidendorfer@gmx.de>
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

#include <assert.h>
#include <stdlib.h>

void dbrew_config_reset(Rewriter* c)
{
    CaptureConfig* cc;
    int i;

    if (c->cc)
        free(c->cc);

    cc = (CaptureConfig*) malloc(sizeof(CaptureConfig));
    for(i=0; i < CC_MAXPARAM; i++)
        cc->par_state[i] = CS_DYNAMIC;
    for(i=0; i < CC_MAXCALLDEPTH; i++)
        cc->force_unknown[i] = False;
    cc->hasReturnFP = False;
    cc->branches_known = False;

    c->cc = cc;
}


CaptureConfig* getCaptureConfig(Rewriter* c)
{
    if (c->cc == 0)
        dbrew_config_reset(c);

    return c->cc;
}

void dbrew_config_staticpar(Rewriter* c, int staticParPos)
{
    CaptureConfig* cc = getCaptureConfig(c);

    assert((staticParPos >= 0) && (staticParPos < CC_MAXPARAM));
    cc->par_state[staticParPos] = CS_STATIC2;
}

/**
 * This allows to specify for a given function inlining depth that
 * values produced by binary operations always should be forced to unknown.
 * Thus, when result is known, it is converted to unknown state with
 * the value being loaded as immediate into destination.
 *
 * Brute force approach to prohibit loop unrolling.
 */
void dbrew_config_force_unknown(Rewriter* r, int depth)
{
    CaptureConfig* cc = getCaptureConfig(r);

    assert((depth >= 0) && (depth < CC_MAXCALLDEPTH));
    cc->force_unknown[depth] = True;
}

void dbrew_config_returnfp(Rewriter* r)
{
    CaptureConfig* cc = getCaptureConfig(r);
    cc->hasReturnFP = True;
}

void dbrew_config_branches_known(Rewriter* r, Bool b)
{
    CaptureConfig* cc = getCaptureConfig(r);
    cc->branches_known = b;
}
