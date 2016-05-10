//!ccflags = -O1

#include <stdio.h>
#include <stdlib.h>

#define _CSRC
#include <test-common.h>

typedef struct {
    long xdiff, ydiff;
    long factor;
} StencilPoint;

typedef struct {
    unsigned long points;
    StencilPoint p[];
} Stencil;

typedef struct {
    long factor;
    long points;
    StencilPoint* p;
} StencilFactor;

typedef struct {
    long factors;
    StencilFactor f[];
} SortedStencil;

Stencil s5 = {5,{{0,0,-4},{-1,0,1},{1,0,1},{0,-1,1},{0,1,1}}};


static
long
test(Stencil* stencil, long** matrix, long x, long y)
{
    long result = 0;

    for (unsigned long i = 0; i != stencil->points; i++)
    {
        StencilPoint p = stencil->p[i];
        result += p.factor * matrix[p.ydiff + y][p.xdiff + x];
    }

    return result;
}

long testConstants[] = { (long) &s5 };

const TestCase testCase = {
    .length = 5,
    .function = test,
    .routineIndex = 3,
    .stackSize = 128,
    .noaliasParams = 3,
    .data = testConstants
};
