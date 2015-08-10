/**
 * Simple x86_64 emulator/re-generator
 * (c) 2015, Josef Weidendorfer, GPLv2+
 */

#include <stdio.h>
#include "spec.h"

typedef int (*i2_func)(int,int);
typedef int (*i2p_func)(int,int*);

// Test 1

/* Compiled with -O0:
 *   55         push  %rbp
 *   48 89 e5   mov   %rsp,%rbp
 *   89 7d ec   mov   %edi,-0x14(%rbp)
 *   89 75 e8   mov   %esi,-0x18(%rbp)
 *   8b 55 ec   mov   -0x14(%rbp),%edx
 *   8b 45 e8   mov   -0x18(%rbp),%eax
 *   01 d0      add   %edx,%eax
 *   89 45 fc   mov   %eax,-0x4(%rbp)
 *   8b 45 fc   mov   -0x4(%rbp),%eax
 *   5d         pop   %rbp
 *   c3         ret
*/
__attribute__ ((noinline))
int sum(int a, int b)
{
    int res = a + b;
    return res;
}

// Test 2
int sum2(int a, int b)
{
    int res = sum(a, b) + b;
    return res;
}

// Test 3
int sum3(int a, int b)
{
    int sum = 0;
    while(a>0) {
        sum += b;
        a--;
    }
    return sum;
}

// Test 4
int arr4[4] = {1,2,3,4};
int test4(int a, int* b)
{
    return b[a];
}

// decode captured code from c1 into c2
void emulateCaptureRun(char* t1, char* t2, Bool use_i2p,
                       int p1, uint64_t p2, int sp1, uint64_t sp2,
                       Rewriter* c1, Rewriter* c2)
{
    int res;

    printf("\nRun emulator for %s, capturing %s:\n", t1, t2);
    res = (int)rewrite(c1, sp1, sp2);
    printf(" Result: %d\n", res);

    printf("\nCaptured code (size %d):\n", capturedCodeSize(c1));
    setFunc(c2, capturedCode(c1));
    setVerbosity(c2, False, False, False);
    decodeBB(c2, capturedCode(c1));
    printCode(c2);
    setVerbosity(c2, True, True, True);

    if (use_i2p) {
        i2p_func f = (i2p_func) capturedCode(c1);
        res = f(p1, (int*) p2);
    }
    else {
        i2_func f = (i2_func) capturedCode(c1);
        res = f(p1, p2);
    }
    printf("Run captured: %s = %d\n", t1, res);
}

void runTest(char* fname, uint64_t f, Bool use_i2p,
             int p1, uint64_t p2, int sp1, uint64_t sp2,
             int runOrig, int runSpec1, int runSpec2)
{
    Rewriter *c1, *c2, *c3;
    char desc[20];
    int res;

    c1 = allocRewriter(200, 20, 1000);
    c2 = allocRewriter(200, 20, 1000);
    c3 = allocRewriter(200, 20, 0);

    configEmuState(c1, 1000);
    useSameStack(c2, c1);

    if (use_i2p) {
        i2p_func ff = (i2p_func) f;
        res = ff(p1, (int*) p2);
    }
    else {
        i2_func ff = (i2_func) f;
        res = ff(p1, p2);
    }
    printf("Run native: %s = %d\n", fname, res);

    setFunc(c1, f);

    if (runOrig)
        emulateCaptureRun(fname, "unmodified", use_i2p, p1,p2,sp1,sp2, c1, c2);

    if (runSpec1) {
        // Specialize for p1
        setRewriteConfig(c1, 0);
        sprintf(desc, "p1=%d fix", sp1);
        emulateCaptureRun(fname, desc, use_i2p, p1,p2,sp1,sp2, c1, c2);

        // Nesting
        setRewriteConfig(c2, 1);
        sprintf(desc, "nested + p2=%ld fix", sp2);
        emulateCaptureRun(fname, desc, use_i2p, p1,p2,sp1,sp2, c2, c3);
    }

    if (runSpec2) {
        // Specialize for p2
        setRewriteConfig(c1, 1);
        sprintf(desc, "p2=%ld fix", sp2);
        emulateCaptureRun(fname, desc, use_i2p, p1,p2,sp1,sp2, c1, c2);

        // Nesting
        setRewriteConfig(c2, 0);
        sprintf(desc, "nested + p1=%d fix", sp1);
        emulateCaptureRun(fname, desc, use_i2p, p1,p2,sp1,sp2, c2, c3);
    }

    // Specialize Par 1 and 2
    setRewriteConfig2(c1, 0,1);
    sprintf(desc, "p1=%d/p2=%ld fix", sp1, sp2);
    emulateCaptureRun(fname, desc, use_i2p, p1,p2,sp1,sp2, c1, c2);
}


int main()
{
    //runTest("sum(4,7)",  sum,  4,7,1,2, 1,1,1);
    //runTest("sum2(4,7)", sum2, 4,7,1,2, 1,1,1);
    //runTest("sum3(4,7)", sum3, 4,7, 3,5, 0,1,0);
    runTest("test4(1,arr4)", (uint64_t) test4, True,
            1, (uint64_t) arr4, 3, (uint64_t) arr4, 0,0,0);
    return 0;
}
