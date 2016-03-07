
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <brew-code-storage.h>


CodeStorage* initCodeStorage(int size)
{
    int fullsize;
    uint8_t* buf;
    CodeStorage* cs;

    /* round up size to multiple of a page size */
    fullsize = (size + 4095) & ~4095;

    /* We do not want to use malloc as we need execute permission.
    * This will return an address aligned to a page boundary
    */
    buf = (uint8_t*) mmap(0, fullsize,
                          PROT_READ | PROT_WRITE | PROT_EXEC,
                          MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (buf == (uint8_t*)-1) {
        perror("Can not mmap code region.");
        exit(1);
    }

    cs = (CodeStorage*) malloc(sizeof(CodeStorage));
    cs->size = size;
    cs->fullsize = fullsize;
    cs->buf = buf;
    cs->used = 0;

    //fprintf(stderr, "Allocated Code Storage (size %d)\n", fullsize);

    return cs;
}

void freeCodeStorage(CodeStorage* cs)
{
    if (cs)
        munmap(cs->buf, cs->fullsize);
    free(cs);
}

/* this checks whether enough storage is available, but does
 * not change <used>.
 */
uint8_t* reserveCodeStorage(CodeStorage* cs, int size)
{
    if (cs->fullsize - cs->used < size) {
        fprintf(stderr,
                "Error: CodeStorage (size %d) too small: used %d, need %d\n",
                cs->fullsize, cs->used, size);
        exit(1);
    }
    return cs->buf + cs->used;
}

uint8_t* useCodeStorage(CodeStorage* cs, int size)
{
    uint8_t* p = cs->buf + cs->used;
    assert(cs->fullsize - cs->used >= size);
    cs->used += size;
    return p;
}
