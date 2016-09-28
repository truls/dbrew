
INTERLINES=32

WFLAGS_BASE=-Wall -Wextra -Wmissing-field-initializers -Wunused-parameter \
			-Wold-style-definition -Wmissing-declarations -Wmissing-prototypes \
			-Wredundant-decls -Wmissing-noreturn -Wshadow -Wpointer-arith \
			-Wwrite-strings -Winline -Wformat-nonliteral -Wformat-security \
			-Wswitch-default -Winit-self -Wnested-externs -Wstrict-prototypes \
			-Wmissing-include-dirs -Wundef -Wmissing-format-attribute \
			-Wswitch-enum -Wswitch -Waggregate-return

CFLAGS=-g -std=c99 -I../include -I../../include/priv -I../../include $(WFLAGS) \
	   -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -D_POSIX_C_SOURCE=200809 \
	   -DSTENCIL_INTERLINES=$(INTERLINES) -O3 -msse2 -msse3 -mssse3
LDFLAGS=-g -L/usr/lib64/llvm
LDLIBS=-lLLVM-3.8  ../../libdbrew.a ../libdbrew-llvm.a

all: benchmark-$(INTERLINES).o benchmark-line-$(INTERLINES).o

benchmark-$(INTERLINES).o:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ benchmark.c timer.c $(LDLIBS)

benchmark-line-$(INTERLINES).o: benchmark-line.c timer.c Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ benchmark-line.c timer.c $(LDLIBS)

.PHONY: clean
clean:
	rm -f $(OBJS) benchmark-*.o

.PHONY: benchmark
benchmark:
	./benchmark.sh
