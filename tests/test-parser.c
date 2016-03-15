#include "dbrew.h"

typedef int (*f1_t)(int);
int f1(int);

int main()
{
    f1_t ff;

    Rewriter* r = brew_new();
    Rewriter* r2 = brew_new();
    dbrew_set_function(r, (uint64_t) f1);
    dbrew_verbose(r, True, True, True);
    dbrew_emulate_capture(r, 1);
    ff = (f1_t) dbrew_generated_code(r);
    DBB* dbb = dbrew_decode(r2, (uint64_t) ff);
    dbrew_print_decoded(dbb);
    return ff(1);
}

