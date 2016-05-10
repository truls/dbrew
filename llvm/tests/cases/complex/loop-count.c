//!ccflags = -O1
// This has to be -O1 to get something useful. Apparently LLVM misses a few
// optimizations when mixed push/pop and memory stack operations are involved.

#define _CSRC
#include <test-common.h>

static
long
test(long value)
{
    // Note that this cannot be optimized to `return value;` but has to be
    // return value < 0 ? 0 : value;
    long result = 0;
    for (long i = 0; i < value; i++, result++);
    return result;
}

const TestCase testCase = {
    .length = 5,
    .function = test,
    .routineIndex = 2,
    .stackSize = 128,
    .noaliasParams = 1
};
