
LLVM_CONFIG=llvm-config
WFLAGS_BASE=-Wall -Wextra -Wmissing-field-initializers -Wunused-parameter \
            -Wold-style-definition -Wmissing-declarations -Wmissing-prototypes \
            -Wredundant-decls -Wmissing-noreturn -Wshadow -Wpointer-arith \
            -Wwrite-strings -Winline -Wformat-nonliteral -Wformat-security \
            -Wswitch-default -Winit-self -Wnested-externs -Wstrict-prototypes \
            -Wmissing-include-dirs -Wundef -Wmissing-format-attribute \
            -Wswitch-enum -Wswitch
WFLAGS_CXX_BASE=-Wall -Wextra -Wmissing-field-initializers \
                -Wmissing-declarations -Winit-self -Waggregate-return \
                -Wredundant-decls -Wshadow -Wpointer-arith \
                -Wwrite-strings -Winline -Wformat-nonliteral -Wformat-security \
                -Wmissing-include-dirs -Wmissing-format-attribute -Wswitch \
                -Wno-shadow -Wno-switch-default -Wno-unused-parameter

ifeq ($(CI),1)
WFLAGS=-Werror $(WFLAGS_BASE)
WFLAGS_CXX=-Werror $(WFLAGS_CXX_BASE)
else
WFLAGS=$(WFLAGS_BASE)
WFLAGS_CXX=$(WFLAGS_CXX_BASE)
endif

CFLAGS=`$(LLVM_CONFIG) --cppflags` -O0 -g -std=c99 -Iinclude -I../include/priv \
       -I../include $(WFLAGS)
CXXFLAGS=`$(LLVM_CONFIG) --cppflags` -O0 -g -std=c++11 -Iinclude $(WFLAGS_CXX)
LDFLAGS = `$(LLVM_CONFIG) --ldflags` -g

# TODO: Switch back to shared library. This is *so* slow and large...
LLVM_MODULES = bitwriter core ipo mcjit scalaropts support vectorize x86codegen
LDLIBS = `$(LLVM_CONFIG) --libs $(LLVM_MODULES)` -lstdc++ -lm -lz -ldl -lpthread -ltinfo
# `$(LLVM_CONFIG) --system-libs`
# LDLIBS = -lLLVM-3.7

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
