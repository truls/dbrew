#include "../spec.h"

typedef int (*f1_t)(int);
int f1(int);

int main()
{
    f1_t ff;

    Rewriter* r = brew_new();
    Rewriter* r2 = brew_new();
    brew_set_function(r, (uint64_t) f1);
    brew_verbose(r, True, True, True);
    brew_emulate_capture(r, 1);
    ff = (f1_t) brew_generated_code(r);
    DBB* dbb = brew_decode(r2, (uint64_t) ff);
    brew_print_decoded(dbb);
    return ff(1);
}

