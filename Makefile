CFLAGS=-g
LDFLAGS=-g

all: test

test: test.o spec.o

test.o: spec.h test.c

spec.o: spec.h spec.c

clean:
	rm -rf *~ *.o test
