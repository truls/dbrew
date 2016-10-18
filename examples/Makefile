EXAMPLES = stencil matrix strcmp simple vector
CPPFLAGS=-I../include
LDLIBS=-L.. -ldbrew

# optimization flags should be the same as for DBrew snippets.
# thus, the Makefile from the top directory overrides OPTS for this.
# this is to avoid e.g. SSE/AVX transition penalties
OPTS=-O2 -mavx
CFLAGS=-std=gnu99 $(OPTS)

## no PIE: flags dependent on compiler/version
ifeq ($(shell $(CC) --version | head -c 3),gcc)
 CFLAGS  += -fno-pie
 ifeq ($(shell expr `$(CC) -dumpversion | cut -f1 -d.` \>= 5),1)
  LDFLAGS += -no-pie
 endif
else ifeq ($(shell $(CC) --version | head -c 5),clang)
 CFLAGS += -fno-pie
else
 $(error Compiler $(CC) not supported)
endif

all: $(EXAMPLES)

stencil: stencil.o ../libdbrew.a

matrix: matrix.o ../libdbrew.a

strcmp: strcmp.o ../libdbrew.a

simple: simple.o ../libdbrew.a

vector: vector.o ../libdbrew.a

clean:
	rm -f *.o *~ $(EXAMPLES)
