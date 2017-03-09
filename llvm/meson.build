# -*- mode: python -*-

cpp_flags = ['-Wall', '-Wextra', '-Wmissing-field-initializers',
             '-Wmissing-declarations', '-Winit-self',
             '-Wredundant-decls', '-Wshadow', '-Wpointer-arith',
             '-Wwrite-strings', '-Winline', '-Wformat-nonliteral', '-Wformat-security',
             '-Wmissing-include-dirs', '-Wmissing-format-attribute', '-Wswitch',
             '-Wno-shadow', '-Wno-switch-default', '-Wno-unused-parameter']

llvm_inc = include_directories('include/', '.')

install_headers('include/dbrew-llvm.h',
                'include/llcommon.h',
                'include/lldecoder.h',
                'include/llengine.h',
                'include/llfunction.h')

subdir('src')
