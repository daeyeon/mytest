# MyTest

A lean, hassle-free testing utility for a little joy, my way.

## Features

- Synchronous and asynchronous test support
- Timeout support for potentially hanging tests
- Test setup and teardown functions
- Test exclusion and skipping
- Command-line options for configuration

## Requirements

- C++17 or later

## Usage

### Writing Tests

`#include "mytest.h"` and use the provided macros:

```cpp
#include "mytest.h"

TEST(TestSuite, SyncTest) {
  ASSERT_EQ(1, 1);
}

TEST_ASYNC(TestSuite, ASyncTest) {
  std::async(std::launch::async, [&done]() {
    EXPECT_EQ(1, 1);
    done(); // Call `done()` passed as a parameter when async completes.
  }).get();
}

TEST(TestSuite, SyncTestTimeout, 1000) { // Set timeout (optional), 1000ms
  TEST_EXPECT_FAILURE();
  // This test is expected to fail by exceeding the 1000ms timeout.
  std::this_thread::sleep_for(std::chrono::seconds(2));
  ASSERT_EQ(1, 0);
}

int main(int argc, char* argv[]) {
  return RUN_ALL_TESTS(argc, argv);
}
```

For more usage examples, see [example.cc](example.cc) and [mytest.h](mytest.h).

### Running Tests

```shell
# This utility provides googletest output style:

[==========] Running 3 test case(s).
[ RUN      ] TestSuite
[ RUN      ] TestSuite:SyncTest
[       OK ] TestSuite:SyncTest
[ RUN      ] TestSuite:ASyncTest
[       OK ] TestSuite:ASyncTest
[ RUN      ] TestSuite:SyncTestTimeout

 Timed out : TestSuite:SyncTestTimeout
    Passed : Expected fail and failed.
[       OK ] TestSuite:SyncTestTimeout
[       OK ] TestSuite
[==========] 3 test case(s) ran.
[  PASSED  ] 3 test(s)
```

### Example

```shell
cmake -B out && make -C out
out/example.x -s
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
  -s            : Silent mode (suppress stdout and stderr output)
  -h, --help    : Show this help message
```

## License

This project is licensed under the Apache License, Version 2.0. See [the LICENSE file](http://www.apache.org/licenses/LICENSE-2.0) for details.
