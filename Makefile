CFLAGS=-g
LDFLAGS=-g

all: test stencil branchtest strcmp

test: test.o spec.o

branchtest: branchtest.o spec.o

stencil: stencil.o spec.o

test.o: spec.h test.c

branchtest.o: spec.h branchtest.c

spec.o: spec.h spec.c

clean:
	rm -rf *~ *.o test stencil
