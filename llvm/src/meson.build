# -*- mode: python -*-

libdbrew_llvm = static_library('dbrew_llvm', 'llbasicblock.c', 'lldecoder.c',
                               'llengine.c', 'llflags.c', 'llfunction.c',
                               'llinstruction.c', 'llinstruction-gp.c', 'llinstruction-stack.c', 'llinstruction-gp.c', 'llinstruction-sse.c', 'llinstruction-callret.c', 'llinstruction-libc.c', 'lloperand.c', 'llsupport.cc', 'llregfile.c',
                               dependencies: [libllvm],  include_directories: [llvm_inc, inc],
                               c_args: [main_lib_args, llvm_cflags],
                               cpp_args: [cpp_flags, llvm_ccflags],
                               pic: false, install: true)
