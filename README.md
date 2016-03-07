## DBrew, the dynamic binary rewriting library

This library allows application-controlled, explicit rewriting of functions
at runtime on the binary level. The rewritten functions can be used instead
of the original functions as drop-in replacements as they use the exact same
function signature. If rewriting fails, there is always a fall-back: calling
the original function.

# Why is this useful?

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


# Supported Architectures

For now just one:
* amd64 (that is, 64bit x86)


# Example

To generate a spezialised version of strcmp which only can compare a given
string with a fixed string, which should be faster than the generic strcmp:

    f = dbrew_rewrite(strcmp, str, "Hello World!");

Use the returned function pointer to run the generated special comparison.
The second parameter actually is not used in the rewritten code. However,
if rewriting failed for whatever reason, the original strcmp may be returned
(depending on configuration). So, it is better to use valid parameters.


# License

LGPLv2.1+


#
