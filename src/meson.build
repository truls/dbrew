sources = ['buffers.c',
           'colors.c',
           'config.c',
           'dbrew.c',
           'decode.c',
           'dgraph.c',
           'emulate.c',
           'engine.c',
           'error.c',
           'expr.c',
           'generate.c',
           'instr.c',
           'introspect.c',
           'printer.c',
           'snippets.c',
           'stubs.c',
           'vector.c',]

libdbrew = static_library('dbrew',
                          sources,
                          dependencies: [libdw, llvm_dep],
                          include_directories: [inc],
                          c_args: main_lib_args,
                          pic: false,
                          install: true)

# TODO: Get rid of this hack
libdbrew_test = static_library('dbrew-test',
                               sources,
                               dependencies: [libdw],
                               include_directories: [inc],
                               c_args: [main_lib_args, '-DTEST_BUILD'],
                               pic: false)
