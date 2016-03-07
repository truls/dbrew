CFLAGS=-g -std=gnu99 -Iinclude \
       -Wall -Wextra \
       -Wmissing-field-initializers -Wunused-parameter -Wold-style-definition \
       -Wmissing-declarations -Wmissing-prototypes -Wredundant-decls \
       -Wmissing-noreturn -Wshadow -Wpointer-arith -Wcast-align \
       -Wwrite-strings -Winline -Wformat-nonliteral -Wformat-security \
       -Wswitch-enum -Wswitch-default -Wswitch -Winit-self \
       -Wmissing-include-dirs -Wundef -Waggregate-return \
       -Wmissing-format-attribute -Wnested-externs -Wstrict-prototypes
LDFLAGS=-g

TEST_PRGS = tests/test tests/branchtest
TEST_OBJS = tests/test.o tests/branchtest.o

EXAMPLE_PRGS = examples/stencil examples/strcmp
EXAMPLE_OBJS = examples/stencil.o examples/strcmp.o


# DBrew (todo: make lib)
SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)
HEADER = $(wildcard include/*.h)


all: $(EXAMPLE_PRGS) $(TEST_PRGS)


# tests (todo: separate Makefile to use different CFLAGS, e.g. -O3)

tests/test.o: tests/test.c $(HEADER)
tests/test: tests/test.o $(OBJS)

tests/branchtest.o: tests/branchtest.c $(HEADER)
tests/branchtest: tests/branchtest.o $(OBJS)


# examples (todo: separate Makefile to use different CFLAGS, e.g. -O3)

examples/stencil.o: examples/stencil.c $(HEADER)
examples/stencil: examples/stencil.o $(OBJS)

examples/strcmp.o: examples/strcmp.c $(HEADER)
examples/strcmp: examples/strcmp.o $(OBJS)


clean:
	rm -rf *~ *.o $(OBJS) \
		$(TEST_OBJS) $(EXAMPLE_OBJS) $(TEST_PRGS) $(EXAMPLE_PRGS)
	(cd tests; make clean)

