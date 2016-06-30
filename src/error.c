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

#include "error.h"

#include "common.h"
#include "printer.h"

#include <assert.h>
#include <stdio.h>

void setErrorNone(Error* e)
{
    e->et = ET_NoError;
}

bool isErrorSet(Error* e)
{
    return (e->et != ET_NoError);
}

void setError(Error* e, Rewriter* r, char* d)
{
    e->et = ET_Unknown;
    e->em = EM_Unknown;
    e->r = r;
    e->desc = d;
}

const char *errorString(Error* e)
{
    static char s[512];

    int o;
    const char* detail = 0;
    const char* module = 0;

    if (e->et == ET_NoError) return "";

    switch(e->em) {
    case EM_Decoder:
        module = "Decoder";
        detail = decodeErrorContext(e);
        break;
    case EM_Unknown: break;
    default: assert(0);
    }

    if (module)
        o = sprintf(s, "%s error", module);
    else
        o = sprintf(s, "Error");

    if (detail)
        o += sprintf(s+o, " at %s", detail);

    if (e->desc)
        o += sprintf(s+o, ": %s", e->desc);
    else
        o += sprintf(s+o, ": no description");

    return s;
}

// add error to some log, with further description (e.g. recover action)
// currently just to as regular output
void logError(Error* e, char* d)
{
    if (d)
        fprintf(stderr, "%s. %s\n", errorString(e), d);
    else
        fprintf(stderr, "%s\n", errorString(e));
}

void setDecodeError(DecodeError* de, Rewriter* r, char* d,
                    ErrorType et, DBB* dbb, int o)
{
    Error* e = (Error*)de;

    setError(e, r, d);
    e->em = EM_Decoder;
    e->et = et;
    de->dbb = dbb;
    de->offset = o;
}

const char *decodeErrorContext(Error* e)
{
    static char buf[100];
    DecodeError* de = (DecodeError*)e;

    assert(e->em == EM_Decoder);
    sprintf(buf, "decoding BB %s+%d",
            prettyAddress(de->dbb->addr, de->dbb->fc), de->offset);

    return buf;
}
