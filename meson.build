# -*- mode: python -*-

project('dbrew', 'c', 'cpp', version: '1.0.0', license: 'LGPL-v2',
        default_options: ['c_std=gnu11', 'cpp_std=gnu++14'])

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

add_global_arguments('-fno-pie', language: 'c')
add_global_link_arguments('-no-pie', language: 'c')
add_global_arguments('-fno-pie', language: 'cpp')
add_global_link_arguments('-no-pie', language: 'cpp')

inc = include_directories('include/', 'include/priv', '.')

# config.h generation
conf = configuration_data()

if get_option('enable_llvm')
message('LLVM support requested')
llvm_conf = find_program('llvm-config-64')
if llvm_conf.found()
llvm_version = run_command(llvm_conf, '--version').stdout()
if not llvm_version.version_compare('>=3.8.1')
error('Requires at least LLVM version 3.8.1')
endif

llvm_inc = run_command(llvm_conf, '--includedir').stdout().strip()
llvm_libs = run_command(llvm_conf, '--libs').stdout().strip()
llvm_lib = cc.find_library('LLVM')
llvm_cflags = run_command(llvm_conf, '--cflags').stdout().strip().split(' ')
llvm_ccflags = run_command(llvm_conf, '--cppflags').stdout().strip().split(' ')
_fc = []
foreach f : llvm_cflags
  # A bit of a hack. TODO: Only filter _FORTIFY_SOURCE
  # when compiling without optimizations
  if f != '' #or f != '-Wp,-D_FORTIFY_SOURCE=2'
    _fc += [f]
  endif
endforeach
llvm_cflags = _fc
_fc = []
foreach p : llvm_ccflags
  if f != ''
    _fc += [f]
  endif
endforeach
llvm_ccflags = _fc

libllvm = declare_dependency(link_args: llvm_libs)
  conf.set('HAVE_LLVM', true)
  subdir('llvm')
  llvm_dep = declare_dependency(link_with: libdbrew_llvm)
else
  error('LLVM config not found (llvm-config-64 not in path)')
  endif

else
libdbrew_llvm = []
llvm_dep = []
endif

libdw = []

#'libdwarf'], native: true)
if get_option('enable_libdw')
  message('libdw support requested')
  libdw = dependency('libdw', required: true)
  conf.set('HAVE_LIBDW', true)
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
