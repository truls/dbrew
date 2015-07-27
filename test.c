/**
 * Simple x86_64 emulator/re-generator
 * (c) 2015, Josef Weidendorfer, GPLv2+
 */

#include <stdio.h>
#include "spec.h"

int sum(int a, int b)
{
  return a + b;
}

typedef int (*sum_func)(int,int);

int main()
{
    uint8_t* captured;
    Code *c1, *c2;

    c1 = allocCode(100, 1000);
    decodeFunc(c1, (uint8_t*) sum, 100, 1);
    printf("Sum Code:\n");
    printCode(c1);

    printf("\nRun emulator:\n");
    initEmulatorState(1024);
    printf("Emulated: 1 + 2 = %d\n", (int)emulate(c1, 1, 2));
    //printf("55 + 12345 = %d\n", (int)emulate(c1, 55, 12345));

    // regenerate code
    captured = capturedCode(c1);
    c2 = allocCode(100, 0);
    decodeFunc(c2, captured, capturedCodeSize(c1), 0);
    printf("\nCaptured code (size %d):\n",
	   capturedCodeSize(c1));
    printCode(c2);
    sum_func cap_func = (sum_func) captured;
    printf("Captured: 1 + 2 = %d\n", cap_func(1, 2));

    // specialized (TODO: does nothing for now)
    sum_func f = (sum_func) spec2((uint8_t*) sum, 1, 1);
    printf("\nSpec: 1 + 2 = %d\n", f(1, 2));

    return 0;
}
