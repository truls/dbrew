CFLAGS=-g
LDFLAGS=-g

all: test

test: test.o spec.o

clean:
	rm -rf *~ *.o test
