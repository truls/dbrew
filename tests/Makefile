TESTPROGS = test branchtest op-tls
CPPFLAGS=-I../include
LDLIBS=-L.. -ldbrew
CFLAGS=-O2

all: $(TESTPROGS)

op-tls: op-tls.o test-parser.o

clean:
	rm -f *.o *~ $(TESTPROGS) test-op-inc test-op-pxor
