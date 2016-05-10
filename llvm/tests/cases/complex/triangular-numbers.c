
#define _CSRC
#include <test-common.h>

static
long
test(long value)
{
    long result = 0;
    do {
        result += value--;
    } while (value != 0);
    return result;
}

const TestCase testCase = {
    .length = 3,
    .function = test,
    .routineIndex = 2
};
