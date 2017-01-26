# -*- mode: python -*-

#static_library('dbrew', 'src/buffers.c', 'src/config.c', 'src/dbrew.c', 'src/decode.c', 'src/dwarf.c',
#               'src/emulate.c', 'src/engine.c', 'src/error.c', 'src/expr.c', 'src/generate.c', 'src/instr.c',
#               'src/printer.c', 'src/snippets.c', 'src/vector.c', include_directories: [inc])

libdbrew = static_library('dbrew', 'buffers.c', 'config.c', 'dbrew.c', 'decode.c', 'introspect.c',
                          'emulate.c', 'engine.c', 'error.c', 'expr.c', 'generate.c', 'instr.c',
                          'printer.c', 'snippets.c', 'vector.c', dependencies: [libdw, llvm_dep],
                          include_directories: [inc], c_args: main_lib_args, pic: false)

# TODO: Get rid of this hack
libdbrew_test = static_library('dbrew-test', 'buffers.c', 'config.c', 'dbrew.c', 'decode.c', 'introspect.c',
                               'emulate.c', 'engine.c', 'error.c', 'expr.c', 'generate.c', 'instr.c',
                               'printer.c', 'snippets.c', 'vector.c', dependencies: [libdw],
                               include_directories: [inc], c_args: [main_lib_args, '-DTEST_BUILD'],
                               pic: false)
