# Explicitly set to gcc. Still can be overwritten by environment variable
# Reason: later we check for GCC, and 'cc' may be configured not to be gcc (?)
CC=gcc

WFLAGS_BASE=-Wall -Wextra -Wmissing-field-initializers -Wunused-parameter \
            -Wold-style-definition -Wmissing-declarations -Wmissing-prototypes \
            -Wredundant-decls -Wmissing-noreturn -Wshadow -Wpointer-arith \
            -Wwrite-strings -Winline -Wformat-nonliteral -Wformat-security \
            -Wswitch-default -Winit-self -Wnested-externs -Wstrict-prototypes \
            -Wmissing-include-dirs -Wundef -Wmissing-format-attribute
ifeq ($(CI),1)
WFLAGS=-Werror $(WFLAGS_BASE)
else
WFLAGS=$(WFLAGS_BASE)
endif

WFLAGS2=-Wswitch-enum -Wswitch -Waggregate-return

CFLAGS=-g -std=gnu99 -Iinclude -Iinclude/priv $(WFLAGS)
LDFLAGS=-g

# always compile examples and DBrew snippets with optimizations
# this allows other DBrew code to still be debuggable
OPTFLAGS=-O2 -mavx

# some snippets 'switch' to AVX mode. hack to avoid 32-byte stack alignment
# FIXME: rewriter should move such stack alignment to outermost level
# clang does not know these options
ifeq ($(CC),gcc)
SNIPPETSFLAGS=$(OPTFLAGS) -mno-vzeroupper -mpreferred-stack-boundary=5
else
SNIPPETSFLAGS=$(OPTFLAGS)
endif

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)
HEADERS = $(wildcard include/*.h)

SUBDIRS=tests examples
.PHONY: $(SUBDIRS)

all: libdbrew.a $(SUBDIRS)

libdbrew.a: $(OBJS)
	ar rcs libdbrew.a $(OBJS)

test: libdbrew.a
	$(MAKE) test -C tests

src/snippets.o: src/snippets.c
	$(CC) $(CFLAGS) $(SNIPPETSFLAGS) -c $< -o $@

examples: libdbrew.a
	cd examples && $(MAKE) OPTS='$(OPTFLAGS)'

clean:
	rm -rf *~ *.o $(OBJS) libdbrew.a
	$(MAKE) clean -C tests
	cd examples && make clean
