
#ifndef TEST_CASE_COMMON_H
#define TEST_CASE_COMMON_H
#ifdef _CSRC

struct TestCase {
    long length;
    void* function;
    long routineIndex;
    long stackSize;
    long noaliasParams;
    void* data;
    long fixFirstParam;
    long fixedParamData;
    long fixedParamLength;
} __attribute__((packed));

typedef struct TestCase TestCase;

#endif
// #error

#define TEST_DRIVER_INT_ARRAY 0
#define TEST_DRIVER_DOUBLE_ARRAY 1
#define TEST_DRIVER_INT 2
#define TEST_DRIVER_STENCIL_INT 3
#define TEST_DRIVER_STENCIL_DOUBLE 4
#define TEST_DRIVER_FLOAT_ARRAY 5

#endif
