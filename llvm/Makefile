
WFLAGS_BASE=-Wall -Wextra -Wmissing-field-initializers -Wunused-parameter \
            -Wold-style-definition -Wmissing-declarations -Wmissing-prototypes \
            -Wredundant-decls -Wmissing-noreturn -Wshadow -Wpointer-arith \
            -Wwrite-strings -Winline -Wformat-nonliteral -Wformat-security \
            -Wswitch-default -Winit-self -Wnested-externs -Wstrict-prototypes \
            -Wmissing-include-dirs -Wundef -Wmissing-format-attribute \
            -Wswitch-enum -Wswitch -Waggregate-return
ifeq ($(CI),1)
WFLAGS=-Werror $(WFLAGS_BASE)
else
WFLAGS=$(WFLAGS_BASE)
endif

CFLAGS=-g -std=c99 -Iinclude -I../include/priv -I../include $(WFLAGS) \
       -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -D_POSIX_C_SOURCE=2
LDFLAGS=-g

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)
HEADERS = $(wildcard include/*.h)

.PHONY: all clean test

all: libdbrew-llvm.a

libdbrew-llvm.a: $(OBJS) $(HEADERS)
	ar rcs $@ $^

clean:
	rm -f $(OBJS) libdbrew-llvm.a

test: libdbrew-llvm.a
	cd tests && ./test.py
