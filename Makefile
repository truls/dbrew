CFLAGS=-g
LDFLAGS=-g

PRGS = test stencil branchtest strcmp

all: $(PRGS)


test.o: test.c spec.h

test: test.o spec.o


strcmp.o: strcmp.c spec.c


branchtest.o: branchtest.c spec.h

branchtest: branchtest.o spec.o


stencil.o: stencil.c spec.h

stencil: stencil.o spec.o



spec.o: spec.h spec.c

clean:
	rm -rf *~ *.o $(PRGS)

