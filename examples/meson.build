# -*- mode: python -*-

examples = ['matrix',
            'simple',
            'stencil',
            'strcmp',
            'vector']

foreach e : examples
  executable(e, ''.join([e, '.c']), include_directories: inc, link_with: libdbrew)
endforeach

#executable('snippets' , 'snippets.c', include_directories: inc, link_with: libdbrew,
#           c_args: ['-mno-vzeroupper', '-mpreferred-stack-boundary=5'])
