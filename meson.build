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

pkgconf_libs = []

# config.h generation
conf = configuration_data()

# FIXME: Get rid of this mess. Some progress here: https://github.com/mesonbuild/meson/issues/1611
llvm_libs = ''
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
    libllvm = declare_dependency(link_args: llvm_libs)
    conf.set('HAVE_LLVM', true)
    subdir('llvm')
    llvm_dep = declare_dependency(link_with: libdbrew_llvm)

    pkgconf_libs += [libdbrew_llvm, llvm_libs, '-lstdc++']
  else
    error('LLVM config not found (llvm-config-64 not in path)')
  endif
else
  libdbrew_llvm = []
  llvm_dep = []
endif

libdw = []
if get_option('enable_libdw')
  message('libdw support requested')
  libdw = dependency('libdw', required: true)
  conf.set('HAVE_LIBDW', true)
  pkgconf_libs += ['-ldw', '-lelf']
endif

config_h = configure_file(input: 'config.h.in',
                          output: 'config.h',
                          configuration: conf)


subdir('src')
subdir('tests')
subdir('examples')

pkgconf_libs = [libdbrew] + pkgconf_libs

# Install defs
install_headers('include/dbrew.h')
pkg = import('pkgconfig')

pkg.generate(subdirs: '.',
             version: '0.1',
             filebase: 'dbrew',
             name: 'libdbrew',
             description: 'A library for performing dynamic binary rewriting',
             libraries: pkgconf_libs)
