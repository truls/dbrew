# DBrew Test Framework

Tests are done by running the Python script `test.py`. This iterates over all
test cases in subdirectory `cases` and prints for each test case the result, as
well as a summary. The script returns 0 back to the shell if all tests pass,
and 1 otherwise. From the top source directory, you can do "make test" to run
all tests.

To test a specific test case, specify it on the command line:

    ./test.py cases/decode/it-add.s

Running a test will force the compilation of the test.
Use "-v" for more verbose output on the test runs, such as printing
the compile command.

## Specification of a Test Case

A test case (denoted by $testcase in the following) is the name
of a test source file existing below the `cases/` subdirectory,
ending in `.c`, `.s` or `.S`, for example `cases/decode/it-add.s`.
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
source using line comments starting with "//!config=value". The following
configurations are supported (the 'config' string in comment):

* "driver": a C source file relative to tests/ directory, to include in the
  compilation of this test case. Allows for small test files by splitting off
  same source parts of test sources (e.g. a main() function)
* "cc": overrides the compiler to use. For .s/.c files, this defaults to
  "as"/$CC, respectively (if environment variable CC is not set, to "cc")
* "ccflags": override compile flags to use, defaults to "-std=c99 -g"
* "args": command line options to use when running the test. Default is empty

To see if configuration options are applied correctly for a test case, use

    ./test.py -v $testcase


## Other Command-line options
- `--debug`, `-d`: Pass `--debug` at the end of the arguments for the test script.
- `--run`, `-r`: Run the specified test cases only and print the output to the terminal.
- `--store`, `-s`: Store the standard output of a test in the corresponding expect file. This can be used when the expect file does not exist yet. However, this can be also used to update existing expect files. **Warning: when updating existing test cases, verify the differences to the previous results twice!**
