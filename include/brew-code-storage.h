
#ifndef BREW_CODE_STORAGE
#define BREW_CODE_STORAGE

#include <stdint.h>

// XXX: Move Struct in C file after removing all direct dependencies!
struct _CodeStorage {
    int size;
    int fullsize; /* rounded to multiple of a page size */
    int used;
    uint8_t* buf;
};

typedef struct _CodeStorage CodeStorage;

CodeStorage* initCodeStorage(int size);
void freeCodeStorage(CodeStorage* cs);

/* this checks whether enough storage is available, but does
 * not change <used>.
 */
uint8_t* reserveCodeStorage(CodeStorage* cs, int size);
uint8_t* useCodeStorage(CodeStorage* cs, int size);

#endif
