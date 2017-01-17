# -*- mode: python -*-

project('dbrew', 'c', version: '1.0.0', license: 'LGPL-v2',
        default_options: ['c_std=gnu11'])

# Add warning flags
main_lib_args = ['-Wall', '-Wextra', '-Wmissing-field-initializers',
                 '-Wunused-parameter', '-Wold-style-definition',
                 '-Wmissing-declarations', '-Wmissing-prototypes',
                 '-Wredundant-decls', '-Wmissing-noreturn', '-Wshadow' ,
                 '-Wpointer-arith', '-Wwrite-strings', '-Winline', '-Wformat-nonliteral',
                 '-Wformat-security', '-Wswitch-default', '-Winit-self', '-Wnested-externs',
                 '-Wstrict-prototypes', '-Wmissing-include-dirs', '-Wundef',
                 '-Wmissing-format-attribute']



cc = meson.get_compiler('c')

inc = include_directories('include/', 'include/priv', '.')

if get_option('enable_llvm')
message('LLVM support requested')
llvm_conf = find_program('llvm-config-64')
if llvm_conf.found()
llvm_version = run_command(llvm_conf, '--version').stdout()
  llvm_libs = run_command(llvm_conf, '--libs').stdout()
endif

#llvm = dependency('LLVM')
llvm = cc.find_library('LLVM')
endif

# config.h generation
conf = configuration_data()

dwarf = []

#'libdwarf'], native: true)
if get_option('enable_dwarf')
  message('DWARF support requested')
  dwarf = dependency('libdw', required: true)
  conf.set('HAVE_LIBDWARF', true)
endif

config_h = configure_file(input: 'config.h.in',
                          output: 'config.h',
                          configuration: conf)


subdir('src')
subdir('tests')
subdir('examples')


# Add target for running clang-tidy
# clang_tidy = find_program('clang_tidy', 'git', 'xargs')
# custom_target('tidy', input: ['compile_commands.json'], 
