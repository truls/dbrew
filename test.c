#include <stdio.h>
#include "spec.h"

int sum(int a, int b)
{
  return a + b;
}

typedef int (*sum_func)(int,int);

int main()
{
    Code* c = allocCode(100);
    decodeFunc(c, (void_func)sum, 100, 1);
    printf("Parsed Code:\n");
    printCode(c);

    initEmulatorState(1024);
    printf("1 + 2 = %d\n", (int)emulate(c, 1, 2));
    printf("55 + 12345 = %d\n", (int)emulate(c, 55, 12345));

    // regenerate
    sum_func f = (sum_func)spec2((void_func)sum, 1, 1);
    printf("Spec: 1 + 2 = %d\n", f(1, 2));

    return 0;
}
