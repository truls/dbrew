#include <string.h>
#include "spec.c"

typedef int (*f_t)(char*);

int isHello(char* s)
{
  return strcmp(s, "Hello");
}

int main(int argc, char* argv[])
{
    strcmp("Foo", argv[0]); // trigger lazy relocation of strcmp

    setDefaultVerbosity(True, True, True);
    f_t f = (f_t) rewrite((uint64_t) isHello, "Bla");

    return f(argv[1]);
}
