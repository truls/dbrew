# DBrew Test Framework

Tests are done by running the Python script 'test.py'. This iterates over all
test cases in subdirectory 'cases' and prints for each test case the result, as
well as a summary. The script returns 0 back to the shell if all tests pass,
and 1 otherwise. From the top source directory, you can do "make test" to run
all tests.

To run a specific test case, specify it on the command line. E.g.

    ./test.py cases/decode/it-add.s

Running a test will force the compilation of the test.
Use "-v" for more verbose output on the test runs, such as printing
the compile command.


## Specification of a Test Case

A test case is given by the existance of a test source file below the cases/
subdirectory ending in .c or .s (e.g. testcase = 'cases/decode/it-add.s').
However, such a file often will not be the complete source for a test, see
*driver* configuration below.

The test source will be compiled and linked with DBrew, and the stdout/stderr
output of a run will be compared with the files $testcase.expect and
$testcase.expect_stderr, respectively. If any file with expected output does
not exist, the output will be ignored as being irrelevant for the test result.
Before comparison, output will be passed through a filter script given as
$testcase.expect_filter (or $testcase.expect_stderr_filter) if present. This
allows to suppress variable parts of test outputs before comparing (e.g.
absolute addresses).

### Test Case Configuration Options

Specific test case configuration can be provided in the first lines of a test
source using line comments starting with "//!". The following configurations
are supported:

* "//!driver = <source file>": a C source file to include in the
  compilation of this test case. Allows for minimal files by splitting off
  same source parts of test sources (e.g. a main() function).
* "//!cc": overrides the compiler to use. For .s/.c files, this defaults to
  "as"/$CC, respectively (if environment variable CC is not set, to "cc").

To see if configuration options are applied correctly for a test case, use

    ./test.py -v testcase
