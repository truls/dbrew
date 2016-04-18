//  COMPILE:cc -o %s %s test-parser.c ../libdbrew.a -I../include

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "dbrew.h"

typedef int (*f1_t)(int);
int f1(int);

int main(int argc, char** argv)
{
    f1_t ff;

    int parameter = 1;
    bool debug = argc >= 2 && strcmp(argv[1], "--debug") == 0;

    Rewriter* r = dbrew_new();
    dbrew_set_function(r, (uint64_t) f1);
    dbrew_config_function_setname(r, (uint64_t) f1, "test");

    // Only output DBB and new function but not intermediate steps as the stack
    // pointer is different on each run. However, for debugging we want this
    // information.
    dbrew_verbose(r, true, debug ? true : false, true);

    dbrew_config_staticpar(r, 0);

    ff = (f1_t) dbrew_rewrite(r, parameter);

    // Decode the newly generated function.
    Rewriter* r2 = dbrew_new();
    dbrew_config_function_setname(r2, (uint64_t) ff, "gen");
    DBB* dbb = dbrew_decode(r2, (uint64_t) ff);
    dbrew_print_decoded(dbb);

    // // Ensure that the program actually works.
    // int original = f1(parameter);
    // int rewritten = ff(parameter);

    // printf("!TST Original %d Rewritten %d\n", original, rewritten);

    // if (rewritten != original)
    // {
    //     return 1;
    // }

    return 0;
}
