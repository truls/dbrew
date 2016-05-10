
WFLAGS_BASE=-Wall -Wextra -Wmissing-field-initializers -Wunused-parameter \
			-Wold-style-definition -Wmissing-declarations -Wmissing-prototypes \
			-Wredundant-decls -Wmissing-noreturn -Wshadow -Wpointer-arith \
			-Wwrite-strings -Winline -Wformat-nonliteral -Wformat-security \
			-Wswitch-default -Winit-self -Wnested-externs -Wstrict-prototypes \
			-Wmissing-include-dirs -Wundef -Wmissing-format-attribute \
			-Wswitch-enum -Wswitch -Waggregate-return

CFLAGS=-g -std=c99 -I../include -I../../include/priv -I../../include $(WFLAGS) \
	   -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -D_POSIX_C_SOURCE=200809 -O2#3 -march=native
LDFLAGS=-g -L/usr/lib64/llvm -lLLVM-3.7.0

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)
HEADERS = $(wildcard *.h)

all: benchmark

benchmark: $(OBJS) ../../libdbrew.a ../libdbrew-llvm.a

clean:
	rm -f $(OBJS) benchmark
