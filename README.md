# DBrew - a Library for Dynamic Binary Rewriting

[![Build Status](https://travis-ci.org/lrr-tum/dbrew.svg?branch=master)](https://travis-ci.org/lrr-tum/dbrew)

This library allows application-controlled, explicit rewriting of functions
at runtime on the binary level. The rewritten functions can be used instead
of the original functions as drop-in replacements as they use the exact same
function signature. If rewriting fails, there is always a fall-back: calling
the original function.

Warning: DBrew is in a very early state with lots of features missing.

## Why is this useful?

Performance
* specialization: if function parameters are known at runtime
* inline functions
* optimize for common case: reorder, inline

Change Functionality
* redirect function calls, memory accesses
* replace instructions (eg. to use other ISA revision)
* insert instrumentation for profiling

Any rewriter configuration relates to properties of a function interface.
Relying on the C calling convention of the target architecture (its ABI)
enables rewriting of compiled code from most languages (C, C++, ...) as
well as architecture independence of the DBrew API.


## Supported Architectures

For now just one:
* amd64 (that is, 64bit x86)


## Example

To generate a spezialised version of strcmp which only can compare a given
string with a fixed string, which should be faster than the generic strcmp:

    strcmpHW = dbrew_rewrite(strcmp, str, "Hello World!");

Use the returned function pointer to run the generated special comparison.
The second parameter actually is not used in the rewritten code. However,
if rewriting failed for whatever reason, the original strcmp may be returned
(depending on configuration). So, it is better to use valid parameters.

FIXME: This short example currently does not work because DBrew does not yet
(1) catch 1st-time invokation of runtime linker for shared library functions,
(2) it does not specialize on mixed knowledge (known/unknown) of SSE/AVX
registers, which the strcmp version in your glibc may use.

## License

LGPLv2.1+


## Publications

* Josef Weidendorfer and Jens Breitbart. The Case for Binary Rewriting at Runtime for Efficient Implementation of High-Level Programming Models in HPC. In *Proceedings of the 21st int. Workshop on High-Level Parallel Programming Models and Supportive Environments (HIPS 2016)*. Chicago, US, 2016. ([PDF of pre-print version](https://github.com/lrr-tum/dbrew/raw/master/docs/pubs/preprint-hips16.pdf))

