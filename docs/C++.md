The DBrew C API can be used from C++.
However, there are some details to care about.

We will provide a C++ API in the future.

## Reference Parameters

Using C++ references as parameter type is not straight-forward, as the C API does not know about references.
So, this does not work:

```
typedef int (*foo_t)(const int&, int);

int foo(const int &i, int j) {...}

foo_t f = (foo_t) dbrew_rewrite(r, 2, 3);
```

But you can cast dbrew_rewrite to the matching C++-signature.

```
typedef int64_t (*rewrite_foo_t)(Rewriter*, const int&, int);
...
foo_t f = (foo_t) ((rewrite_foo_t)dbrew_rewrite)(r, 2, 3);
```

In binary code, C++ references are pointers. Now, if you mark
a C++ reference as known, DBrew will interpret this as the
corresponding pointer to be known. As the semantic of passing
a pointer as known includes that all values reachable via that
pointer also are handled as known, DBrew shows the expected
behavior, i.e. the value behind the reference will be handled
as known.
