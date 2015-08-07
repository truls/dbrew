/**
 * Simple x86_64 emulator/re-generator
 * (c) 2015, Josef Weidendorfer, GPLv2+
 */

#include <stdio.h>
#include "spec.h"

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


typedef int (*sum_func)(int,int);

// decode captured code from c1 into c2
void emulateCaptureRun(char* t1, char* t2, char* t3, Code* c1, Code* c2)
{
    int res;
    sum_func f;

    printf("\nRun emulator for %s, capturing %s:\n", t1, t2);
    res = (int)emulate(c1, 1, 2);
    printf(" Result: %d\n", res);

    printf("\nCaptured code (size %d):\n", capturedCodeSize(c1));
    setFunc(c2, capturedCode(c1));
    decodeBB(c2, capturedCode(c1));
    printCode(c2);

    f = (sum_func) capturedCode(c1);
    res = f(4, 7);
    printf("Run captured: %s = %d\n", t3, res);
}

int main()
{
    Code *c1, *c2, *c3;
    int res;

    c1 = allocCode(200, 20, 1000);
    c2 = allocCode(200, 20, 1000);
    c3 = allocCode(200, 20, 0);

    configEmuState(c1, 1000);
    useSameStack(c2, c1);

    res = sum(1,2);
    printf("Run native: 1 + 2 = %d\n", res);

    setFunc(c1, (uint64_t)sum3);

    emulateCaptureRun("sum(1,2)", "unmodified", "4 + 7", c1, c2);

    // Specialize sum for par 1
    setCaptureConfig(c1, 0);
    emulateCaptureRun("sum(1,2)", "with par1=1", "4 (1) + 7", c1, c2);

    // Nesting
    setCaptureConfig(c2, 1);
    emulateCaptureRun("sum1(x,2)", "with par2=2", "4 (1) + 7 (2)", c2, c3);

    // Specialize sum for par 2
    setCaptureConfig(c1, 1);
    emulateCaptureRun("sum(1,2)", "with par2=2", "4 + 7 (2)", c1, c2);

    // Nesting
    setCaptureConfig(c2, 0);
    emulateCaptureRun("sum2(1,x)", "with par1=1", "4 (1) + 7 (2)", c2, c3);

    // Specialize Par 1 and 2
    setCaptureConfig2(c1, 0,1);
    emulateCaptureRun("sum(1,2)", "with par1/2=1/2", "4 (1) + 7 (2)", c1, c2);

    // Nesting (should do nothing)
    setCaptureConfig2(c2, 0,1);
    emulateCaptureRun("sum12(x,x)", "with par1/2=1/2", "4 (1) + 7 (2)", c2, c3);

    return 0;
}
