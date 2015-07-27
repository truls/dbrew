#include <stdio.h>
#include "spec.h"

int sum(int a, int b)
{
  return a + b;
}

typedef int (*sum_func)(int,int);

int main()
{
  sum_func f = (sum_func)spec2((void_func)sum, 1, 1);
  return printf("2 + 1 = %d\n", f(2,1));
}
