# DBrew - a Library for Dynamic Binary Rewriting

[![Build Status](https://travis-ci.org/lrr-tum/dbrew.svg?branch=master)](https://travis-ci.org/lrr-tum/dbrew)

This library allows application-controlled, explicit rewriting of functions
at runtime on the binary level. The rewritten functions can be used instead
of the original functions as drop-in replacements as they use the exact same
function signature.

Warning: DBrew is in a very early state with lots of features missing.


## Why is this useful?

Performance improvement
* specialization: if function parameters are known at runtime
* optimization of common case by reordering and inline, e.g. when
  profiling/usage data is available

Change functionality
* redirect function calls, memory accesses
* replace instructions
* insert instrumentation for profiling


# API

DBrew provides best-effort and robustness. The API is designed in a way
that rewriting may fail; however, it always can return the original
function as fall-back. Thus, there is no need to strive for complete
coverage of binary code.

Rewriting configurations heavily rely on the C calling convention / ABI
(Application Binary Interface) of the target architecture. This way,
DBrew supports rewriting of compiled code from most languages (C, C++, ...)
and makes the DBrew API itself architecture independent.



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
(1) catch/ignore the dynamic-linker part of 1st-time invocations of shared
library functions and (2) specialize on (mixed) knowledge (known/unknown) about
SSE/AVX registers contents, which the strcmp version in your glibc may use.


## Publications

* Josef Weidendorfer and Jens Breitbart. The Case for Binary Rewriting at Runtime for Efficient Implementation of High-Level Programming Models in HPC. In *Proceedings of the 21st int. Workshop on High-Level Parallel Programming Models and Supportive Environments (HIPS 2016)*. Chicago, US, 2016. ([PDF of pre-print version](https://github.com/lrr-tum/dbrew/raw/master/docs/pubs/preprint-hips16.pdf))


## License

LGPLv2.1+

