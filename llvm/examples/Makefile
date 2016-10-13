
LLVM_CONFIG=llvm-config

WFLAGS_BASE=-Wall -Wextra -Wmissing-field-initializers -Wunused-parameter \
			-Wold-style-definition -Wmissing-declarations -Wmissing-prototypes \
			-Wredundant-decls -Wmissing-noreturn -Wshadow -Wpointer-arith \
			-Wwrite-strings -Winline -Wformat-nonliteral -Wformat-security \
			-Wswitch-default -Winit-self -Wnested-externs -Wstrict-prototypes \
			-Wmissing-include-dirs -Wundef -Wmissing-format-attribute \
			-Wswitch-enum -Wswitch -Waggregate-return

LLVM_MODULES = bitwriter core ipo mcjit scalaropts support vectorize x86codegen

CFLAGS=-g -std=c99 -I../include -I../../include/priv -I../../include $(WFLAGS) \
	   `$(LLVM_CONFIG) --cppflags` -O3 -msse2 -msse3 -mssse3
LDFLAGS = `$(LLVM_CONFIG) --ldflags` -g
LDLIBS = `$(LLVM_CONFIG) --libs $(LLVM_MODULES)` -lstdc++ -lm -lz -ldl -lpthread -ltinfo

all: spmv

spmv: spmv.c ../benchmark/timer.c ../../libdbrew.a ../libdbrew-llvm.a

.PHONY: clean
clean:
	rm -f spmv
