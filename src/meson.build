# -*- mode: python -*-

sources = ['buffers.c', 'config.c', 'dbrew.c', 'decode.c', 'introspect.c',
                          'emulate.c', 'engine.c', 'error.c', 'expr.c', 'generate.c', 'instr.c',
                          'printer.c', 'snippets.c', 'vector.c', 'colors.c']

libdbrew = static_library('dbrew', sources, dependencies: [libdw, llvm_dep],
                          include_directories: [inc], c_args: main_lib_args, pic: false)

# TODO: Get rid of this hack
libdbrew_test = static_library('dbrew-test', sources, dependencies: [libdw],
                               include_directories: [inc], c_args: [main_lib_args, '-DTEST_BUILD'],
                               pic: false)
