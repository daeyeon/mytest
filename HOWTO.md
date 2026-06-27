# How-to Guides

## Contents

- [Add to a Test Binary](#add-to-a-test-binary)
- [Write Assertions](#write-assertions)
- [Run Setup and Cleanup Hooks](#run-setup-and-cleanup-hooks)
- [Skip a Test](#skip-a-test)
- [Mark a Test as Expected to Fail](#mark-a-test-as-expected-to-fail)
- [Set a Per-test Timeout](#set-a-per-test-timeout)
- [Isolate a Test in a Separate Process](#isolate-a-test-in-a-separate-process)
- [Run Every Test in a Separate Process](#run-every-test-in-a-separate-process)
- [Filter Tests](#filter-tests)
- [Suppress Test Output](#suppress-test-output)
- [Disable Color Output](#disable-color-output)
- [Use a Per-test Temporary Directory](#use-a-per-test-temporary-directory)
- [Generate a GTest XML Report](#generate-a-gtest-xml-report)

## Add to a Test Binary

Add `include` to the compiler include path and include `mytest.h`.

```cpp
#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>

TEST(Smoke, Runs) {
  ASSERT(true);
}
```

`MYTEST_CONFIG_USE_MAIN` creates a `main()` that calls
`RUN_ALL_TESTS(argc, argv)`. For a custom `main()`:

```cpp
#include <mytest.h>

int main(int argc, char* argv[]) {
  return RUN_ALL_TESTS(argc, argv);
}
```

## Write Assertions

Use `ASSERT_*` to stop the test on failure. Use `EXPECT_*` to keep running and
report more than one failure.

```cpp
TEST(Assertions, Values) {
  ASSERT_EQ(4, 2 + 2);
  EXPECT_NE(5, 2 + 2);
  ASSERT(true);
  EXPECT(true);
}
```

## Run Setup and Cleanup Hooks

Hooks are registered per group. The group is the first argument to `TEST`.

```cpp
static int counter = 0;

TEST_BEFORE(Counter) {
  counter = 0;
}

TEST_BEFORE_EACH(Counter) {
  ++counter;
}

TEST_AFTER_EACH(Counter) {
  ASSERT(counter > 0);
}

TEST_AFTER(Counter) {
  counter = 0;
}

TEST(Counter, First) {
  ASSERT_EQ(counter, 1);
}
```

Use `TEST_AFTER_ALL(group)` for cleanup that should also run for isolated-only
groups.

## Skip a Test

Call `TEST_SKIP()` or `TEST_SKIP("reason")`.

```cpp
TEST(Feature, DisabledCase) {
  TEST_SKIP("waiting for dependency");
  ASSERT(false);
}
```

By default, execution stops at the skip point. The `-f` CLI option keeps
running.

## Mark a Test as Expected to Fail

Call `TEST_EXPECT_FAILURE()` before the expected failure.

```cpp
TEST(Parser, RejectsInvalidInput) {
  TEST_EXPECT_FAILURE();
  ASSERT_EQ(Parse("bad input"), true);
}
```

Failure becomes success. Success becomes failure.

## Set a Per-test Timeout

Pass a timeout in milliseconds as the optional third argument.

```cpp
TEST(Network, TimesOut, 1000) {
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::seconds(2));
}
```

The default is 60000 ms. Override it for one run with the `-t` CLI option.

## Isolate a Test in a Separate Process

Use `TEST_ISOLATE` for tests that may crash or mutate process-global state.

```cpp
static int global_counter = 0;

TEST_ISOLATE(Process, MutatesGlobal) {
  global_counter = 100;
  ASSERT_EQ(global_counter, 100);
}

TEST(Process, ParentStillClean) {
  ASSERT_EQ(global_counter, 0);
}
```

Output is forwarded in real time. Crashes are reported by signal.

## Run Every Test in a Separate Process

Use the `-j` CLI option to run every selected test in a separate process:

```shell
./test.x -j
```

Use files, sockets, or shared memory when job-mode tests need shared state.

## Filter Tests

Use the `-p PATTERN` CLI option to include tests whose full name matches a
regular expression. Test names have the form `Group:Name`.

```shell
./test.x -p "^Parser:"
./test.x -p "^Parser:RejectsInvalidInput$"
```

Use the `-p -PATTERN` CLI option to exclude matches:

```shell
./test.x -p "-Slow"
```

Exclusions take precedence.

## Suppress Test Output

Use the `-s` CLI option to hide stdout and stderr from test bodies.

```shell
./test.x -s
```

For scoped suppression:

```cpp
TEST(Output, HideNoisySection) {
  printf("visible\n");
  {
    MyTest::SilenceScope silence;
    printf("hidden\n");
  }
  printf("visible again\n");
}
```

## Disable Color Output

Use the `-c` CLI option when comparing output in snapshots or logs:

```shell
./test.x -c
```

## Use a Per-test Temporary Directory

Call `TEST_TEMP_PATH()` to get a directory for the current test.

```cpp
TEST(Files, WritesTemporaryData) {
  auto path = TEST_TEMP_PATH() / "data.txt";
  std::ofstream(path) << "hello";
  ASSERT(std::filesystem::exists(path));
}
```

Repeated calls in the same test return the same path. The directory is removed
after the test, including isolated tests, timeouts, and crashes.

## Generate a GTest XML Report

Include `mytest-report.h`, register a reporter, and run with the `-r` CLI
option.

```cpp
#include <mytest.h>
#include <mytest-report.h>

int main(int argc, char* argv[]) {
  SET_REPORTER(mytest::GTestXmlReporter);
  return RUN_ALL_TESTS(argc, argv);
}
```

```shell
./test.x -r
```

```shell
./test.x -r report.xml
```

The default path is `test_report.xml`.
