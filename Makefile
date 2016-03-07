CFLAGS=-g -std=gnu99 -Iinclude -Wall -Wextra  -Wmissing-field-initializers -Wunused-parameter -Wold-style-definition -Wmissing-declarations -Wmissing-prototypes -Wredundant-decls -Wmissing-noreturn -Wshadow -Wpointer-arith -Wcast-align -Wwrite-strings -Winline -Wformat-nonliteral -Wformat-security -Wswitch-enum -Wswitch-default -Wswitch -Winit-self -Wmissing-include-dirs -Wundef -Waggregate-return -Wmissing-format-attribute -Wnested-externs -Wstrict-prototypes
LDFLAGS=-g

PRGS = test stencil branchtest strcmp

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)
HEADER = $(wildcard include/*.h)

all: $(PRGS)

test.o: test.c $(HEADER)
test: test.o $(OBJS)

branchtest.o: branchtest.c $(HEADER)
branchtest: branchtest.o $(OBJS)

stencil.o: stencil.c $(HEADER)
stencil: stencil.o $(OBJS)

strcmp.o: strcmp.c $(HEADER)
strcmp: strcmp.o $(OBJS)

clean:
	rm -rf *~ *.o $(PRGS) $(OBJS)
