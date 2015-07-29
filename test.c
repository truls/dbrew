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
    uint8_t *captured;
    sum_func cap_func;
    Code *c1, *c2;
    int res;

    res = sum(1,2);
    printf("Native: 1 + 2 = %d (counter %d)\n", res, counter);

    c1 = allocCode(100, 1000);
    decodeFunc(c1, (uint8_t*) sum, 100, 1);
    printf("Code:\n");
    printCode(c1);

    printf("\nRun emulator for sum(1,2), capturing unmodified:\n");
    res = (int)emulate(c1, 1, 2);
    printf(" Result: %d\n", res);

    c2 = allocCode(100, 0);
    captured = capturedCode(c1);
    decodeFunc(c2, captured, capturedCodeSize(c1), 0);
    printf("\nCaptured code (size %d):\n", capturedCodeSize(c1));
    printCode(c2);

    cap_func = (sum_func) captured;
    res = cap_func(1, 2);
    printf("Run captured: 1 + 2 = %d\n", res);

    setCaptureConfig(c1, 1);
    printf("\nRun emulator capturing specialized par2 = 2:\n");
    res = (int)emulate(c1, 1, 2);
    printf(" Result: %d\n", res);

    captured = capturedCode(c1);
    decodeFunc(c2, captured, capturedCodeSize(c1), 0);
    printf("\nCaptured code (size %d):\n", capturedCodeSize(c1));
    printCode(c2);

    cap_func = (sum_func) captured;
    res = cap_func(3, 4);
    printf("Run captured: 3 + 4 = %d\n", res);

    return 0;
}
