/**
 * Simple x86_64 emulator/re-generator
 * (c) 2015, Josef Weidendorfer, GPLv2+
 */

#include <stdio.h>
#include "spec.h"

int counter = 0;

int sum(int a, int b)
{
    int res = a + b;
    return res;
}

typedef int (*sum_func)(int,int);

int main()
{
    uint8_t* captured;
    Code *c1, *c2;
    int res;

    res = sum(1,2);
    printf("Native: 1 + 2 = %d (counter %d)\n", res, counter);

    c1 = allocCode(100, 1000);
    decodeFunc(c1, (uint8_t*) sum, 100, 1);
    printf("Code:\n");
    printCode(c1);

    printf("\nRun emulator:\n");
    initEmuState(1024);
    res = (int)emulate(c1, 1, 2);
    printf("Emulated: 1 + 2 = %d (counter %d)\n", res, counter);
    //printf("55 + 12345 = %d\n", (int)emulate(c1, 55, 12345));

    // regenerate code
    captured = capturedCode(c1);
    c2 = allocCode(100, 0);
    decodeFunc(c2, captured, capturedCodeSize(c1), 0);
    printf("\nCaptured code (size %d):\n",
	   capturedCodeSize(c1));
    printCode(c2);
    sum_func cap_func = (sum_func) captured;
    res = cap_func(1, 2);
    printf("Run captured: 1 + 2 = %d (counter %d)\n", res, counter);

    // specialized (TODO: does nothing for now)
    sum_func f = (sum_func) spec2((uint8_t*) sum, 1, 1);
    res = f(1, 2);
    printf("\nSpec: 1 + 2 = %d (counter %d)\n", res, counter);

    return 0;
}
