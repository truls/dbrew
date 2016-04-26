//  COMPILE:cc -g -o %s %s test-driver.c ../libdbrew.a -I../include

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "dbrew.h"

typedef int (*f1_t)(int);
int f1(int);

int runtest(Rewriter*r, int parameter, bool doRun)
{
    f1_t ff;

    if (parameter >= 0)
        printf(">>> Testcase known par = %d.\n", parameter);
    else
        printf(">>> Testcase unknown par.\n");

    dbrew_set_function(r, (uint64_t) f1);
    dbrew_config_function_setname(r, (uint64_t) f1, "test");
    if (parameter >= 0)
        dbrew_config_staticpar(r, 0);
    ff = (f1_t) dbrew_rewrite(r, parameter);

    // Decode the newly generated function.
    Rewriter* r2 = dbrew_new();
    dbrew_config_function_setname(r2, (uint64_t) ff, "gen");
    DBB* dbb = dbrew_decode(r2, (uint64_t) ff);
    dbrew_print_decoded(dbb);

    if (!doRun) return 0;

    // Ensure that the program actually works.
    int orig = f1(parameter);
    int rewritten = ff(parameter);

    printf(">>> Run orig/rewritten: %d/%d\n", orig, rewritten);
    return (orig != rewritten) ? 1 : 0;
}

int main(int argc, char** argv)
{
    int parameter;
    int arg = 1;
    int res = 0;
    bool debug = false;
    bool run = false;
    bool var = false; // also generate version with variable parameter?
    while((arg<argc) && (argv[arg][0] == '-')) {
        if (strcmp(argv[arg], "--debug")==0) debug = true;
        if (strcmp(argv[arg], "--run")==0) run = true;
        if (strcmp(argv[arg], "--var")==0) var = true;
        arg++;
    }

    Rewriter* r = dbrew_new();
    // Only output DBB and new function but not intermediate steps as the stack
    // pointer is different on each run. However, for debugging we want this
    // information.
    dbrew_verbose(r, true, debug ? true : false, true);

    if (var)
        res += runtest(r, -1, run);

    // cycle through test cases: implicit first/default is parameter "1"
    parameter = 1;
    while(1) {
        res += runtest(r, parameter, run);

        if (arg<argc)
            parameter = atoi(argv[arg++]);
        else
            break;
    }

    return res;
}
