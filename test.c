#include <stdio.h>
#include "spec.h"

int sum(int a, int b)
{
  return a + b + a;
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
    printf("1 + 2 = %d\n", (int)emulate(c1, 1, 2));
    printf("55 + 12345 = %d\n", (int)emulate(c1, 55, 12345));

    // check regenerated code
    captured = capturedCode(c1);
    c2 = allocCode(100, 0);
    decodeFunc(c2, captured, capturedCodeSize(c1), 0);
    printf("\nCaptured code (size %d):\n",
	   capturedCodeSize(c1));
    printCode(c2);

    // regenerate
    sum_func f = (sum_func) spec2((uint8_t*) sum, 1, 1);
    printf("\nSpec: 1 + 2 = %d\n", f(1, 2));

    return 0;
}
