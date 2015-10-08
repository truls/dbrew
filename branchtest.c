/**
 * Simple x86_64 emulator/re-generator
 * (c) 2015, Josef Weidendorfer, GPLv2+
 *
 * Test branching
 */

#include <stdio.h>
#include "spec.h"

typedef int (*i2_func)(int,int);

__attribute__ ((noinline))
int test1(int a, int b)
{
    if (a == 1) return b;
    return 0;
}

__attribute__ ((noinline))
int test2(int a, int b)
{
    while(a > 0) { b++; a--; }
    return b;
}


// decode captured code from c1 into c2
void emulateCaptureRun(char* t1, char* t2,
                       int p1, uint64_t p2, int sp1, uint64_t sp2,
                       Rewriter* c1, Rewriter* c2)
{
    int res;

    printf("Tracing emulation of %s(%d,%ld) %s:\n", t1, sp1, sp2, t2);
    res = (int)rewrite(c1, sp1, sp2);
    printf("Result from emulation: %d\n", res);

    printf("Rewritten code (size %d bytes):\n", generatedCodeSize(c1));
    setFunc(c2, generatedCode(c1));
    printDecoded(c2, generatedCode(c1), generatedCodeSize(c1));

    i2_func f = (i2_func) generatedCode(c1);
    res = f(p1,p2);

    printf("Run rewritten code %s(%d,%ld) = %d\n", t1, p1, p2, res);
}


/*
 * Test different specializations of a given fucntion <f>.
 * <f> must have signature i2_func (2 int parameters, returns int)
 * or i2p_func (int/pointer parameter, int return), given by <use_i2p>
*/
void runTest(char* fname, uint64_t f, int p1, int p2, int sp1)
{
    Rewriter *c1, *c2;
    int res;

    printf(">>> Testing with function %s\n\n", fname);

    c1 = allocRewriter();
    setVerbosity(c1, True, True, True);
    c2 = allocRewriter();

    i2_func ff = (i2_func) f;
    res = ff(p1, p2);
    printf("Run native: %s(%d,%d) = %d\n", fname, p1, p2, res);

    setFunc(c1, f);

    emulateCaptureRun(fname, "unmodified", p1, p2, sp1, p2, c1, c2);

    resetRewriterConfig(c1);
    setRewriterStaticPar(c1, 0);
    emulateCaptureRun(fname, "p1 fix", p1, p2, sp1, p2, c1, c2);

    freeRewriter(c1);
    freeRewriter(c2);
}


int main()
{
    runTest("test1", (uint64_t) test1, 1,7, 2);
    runTest("test2", (uint64_t) test2, 4,7, 1);
    return 0;
}
