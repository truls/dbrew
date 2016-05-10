//!ccflags = -O3

#include <stdio.h>
#include <stdlib.h>

#define _CSRC
#include <test-common.h>

typedef struct {
    long xdiff, ydiff;
    double factor;
} StencilPoint;

typedef struct {
    unsigned long points;
    StencilPoint p[];
} Stencil;

typedef struct {
    double factor;
    long points;
    StencilPoint* p;
} StencilFactor;

typedef struct {
    long factors;
    StencilFactor f[];
} SortedStencil;

#define COEFF1 (-.2)
#define COEFF2 (.3)

Stencil s5 = {5, {{0,0,-.2},
                  {-1,0,.3},{1,0,.3},{0,-1,.3},{0,1,.3}}};

SortedStencil s5s = {2, {{-.2,1,&(s5.p[0])},{.3,4,&(s5.p[1])}}};


static
long
test(Stencil* stencil, double** matrix, long x, long y)
{
    double result = 0;

    for (unsigned long i = 0; i != stencil->points; i++)
    {
        StencilPoint p = stencil->p[i];
        result += p.factor * matrix[p.ydiff + y][p.xdiff + x];
    }

    unsigned long long* intResult = (unsigned long long*) &result;
    return *intResult;
}

long testConstants[] = { (long) &s5 };

const TestCase testCase = {
    .length = 5,
    .function = test,
    .routineIndex = 4,
    .stackSize = 128,
    .noaliasParams = 3,
    .data = testConstants
};
