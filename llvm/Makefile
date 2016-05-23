
LLVM_CONFIG=llvm-config
WFLAGS_BASE=-Wall -Wextra -Wmissing-field-initializers -Wunused-parameter \
            -Wold-style-definition -Wmissing-declarations -Wmissing-prototypes \
            -Wredundant-decls -Wmissing-noreturn -Wshadow -Wpointer-arith \
            -Wwrite-strings -Winline -Wformat-nonliteral -Wformat-security \
            -Wswitch-default -Winit-self -Wnested-externs -Wstrict-prototypes \
            -Wmissing-include-dirs -Wundef -Wmissing-format-attribute \
            -Wswitch-enum -Wswitch -Waggregate-return
ifeq ($(CI),1)
WFLAGS=-Werror $(WFLAGS_BASE)
LDFLAGS = `llvm-config-3.7 --ldflags` -g
else
WFLAGS=$(WFLAGS_BASE)
endif

CFLAGS=`llvm-config --cflags` -O0 -g -std=c99 -Iinclude -I../include/priv \
       -I../include $(WFLAGS)
CXXFLAGS=`llvm-config --cflags` -O0 -g -std=c++0x -Iinclude $(WFLAGS)
LDFLAGS = `$(LLVM_CONFIG) --ldflags` -g
LDLIBS = -lLLVM-3.7
#`llvm-config --libs core analysis mcjit executionengine`

SRCS_C = $(wildcard src/*.c)
SRCS_CXX = $(wildcard src/*.cc)
OBJS_C = $(SRCS_C:.c=.o)
OBJS_CXX = $(SRCS_CXX:.cc=.o)
HEADERS = $(wildcard include/*.h)

.PHONY: all clean test

all: libdbrew-llvm.a

libdbrew-llvm.a: $(OBJS_C) $(OBJS_CXX) $(HEADERS)
	ar rcs $@ $^

clean:
	rm -f $(OBJS_C) $(OBJS_CXX) libdbrew-llvm.a

test: libdbrew-llvm.a
	export LDFLAGS="$(LDFLAGS)" && export LDLIBS="$(LDLIBS)" && cd tests && ./test.py
