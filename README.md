# MyTest

Lean, hassle-free testing utility, my way.

## Features

- Timeout, lifecycle hook, skip, and exclude controls
- Process isolation for individual tests or full runs
- Per-test temp directories with automatic cleanup
- Pluggable reporting (e.g., gtest XML) and CLI configuration

## Requirements

- C++17 or later

## Usage

### Writing Tests

`#include "mytest.h"` and use the provided macros:

```cpp
#include "mytest.h"

TEST(TestSuite, Basic) { // default timeout: 60000ms
  ASSERT_EQ(1, 1);
}

TEST(TestSuite, Timeout, 1000) {  // timeout: 1000ms
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  EXPECT_EQ(1, 0);
}

TEST_ISOLATE(TestSuite, SegfaultIsolated) {
  int* p = nullptr;
  *p = 1; // Intentional crash in a spawned process
}

int main(int argc, char* argv[]) {
  return RUN_ALL_TESTS(argc, argv);
}
```

For more usage examples, see [example.cc](test/00-example/example.cc) and [mytest.h](include/mytest.h).

### Running Tests

```shell
# This utility provides googletest output style:

[==========] Running 3 test case(s).
[ RUN      ] TestSuite
[ RUN      ] TestSuite:Basic
[       OK ] TestSuite:Basic
[ RUN      ] TestSuite:Timeout

 Timed out : TestSuite:Timeout
    Passed : Expected fail and failed.
[       OK ] TestSuite:Timeout
[ RUN      ] TestSuite:SegfaultIsolated (PID: 16502)

Terminated by signal 11 (Segmentation fault)

[  FAILED  ] TestSuite:SegfaultIsolated
[  FAILED  ] TestSuite
[==========] 3 test case(s) ran.
[  PASSED  ] 2 test(s)
[  FAILED  ] 1 test(s)
```

### Example

```shell
cmake -B out && make -C out
out/example.x
```

### Command-line options

The following options are available:

```shell
out/example.x -h

Options:
  -p "PATTERN"  : Include tests matching PATTERN
  -p "-PATTERN" : Exclude tests matching PATTERN
  -t TIMEOUT    : Set the timeout value in milliseconds (default: 60000)
  -c            : Disable color output
  -f            : Force mode, run all tests, including skipped ones
  -j            : Run selected tests separately, one process each
  -l            : List selected tests without running them
  -s            : Silent mode (suppress stdout and stderr output)
  -r [FILE]     : Write report via registered reporter (optional FILE)
  -h, --help    : Show this help message
```

## License

This project is licensed under the Apache License, Version 2.0. See [the LICENSE file](http://www.apache.org/licenses/LICENSE-2.0) for details.
