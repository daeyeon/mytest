# Reference

## Contents

- [Runner Entry Points](#runner-entry-points)
  - [`MYTEST_CONFIG_USE_MAIN`](#mytest_config_use_main)
  - [`RUN_ALL_TESTS(argc, argv)`](#run_all_testsargc-argv)
- [Test Names](#test-names)
- [Test Definition Macros](#test-definition-macros)
  - [`TEST(group, name[, timeout_ms])`](#testgroup-name-timeout_ms)
  - [`TEST_ISOLATE(group, name[, timeout_ms])`](#test_isolategroup-name-timeout_ms)
- [Assertions](#assertions)
  - [`ASSERT_EQ(x, y)` / `ASSERT_NE(x, y)`](#assert_eqx-y--assert_nex-y)
  - [`EXPECT_EQ(x, y)` / `EXPECT_NE(x, y)`](#expect_eqx-y--expect_nex-y)
  - [`ASSERT(cond)` / `EXPECT(cond)`](#assertcond--expectcond)
  - [Failure Output](#failure-output)
- [Utility Macros](#utility-macros)
  - [`TEST_EXPECT_FAILURE([message])`](#test_expect_failuremessage)
  - [`TEST_NAME()`](#test_name)
  - [`TEST_SKIP([message])`](#test_skipmessage)
  - [`TEST_TEMP_PATH()`](#test_temp_path)
- [Hooks](#hooks)
  - [Execution Model](#execution-model)
  - [Hook Failures](#hook-failures)
  - [`TEST_BEFORE(group)`](#test_beforegroup)
  - [`TEST_BEFORE_EACH(group)`](#test_before_eachgroup)
  - [`TEST_AFTER_EACH(group)`](#test_after_eachgroup)
  - [`TEST_AFTER(group)`](#test_aftergroup)
  - [`TEST_AFTER_ALL(group)`](#test_after_allgroup)
- [Timeouts](#timeouts)
- [Command-line Options](#command-line-options)
- [Test Runner API](#test-runner-api)
  - [`MyTest::Instance()`](#mytestinstance)
  - [`MyTest::IsMainProcess()`](#mytestismainprocess)
  - [`MyTest::SetTempRoot(std::filesystem::path)`](#mytestsettemprootstdfilesystempath)
  - [`MyTest::SilenceScope`](#mytestsilencescope)
- [Output and Failure Summary](#output-and-failure-summary)
- [Extensions](#extensions)
  - [Test Helper Extensions](#test-helper-extensions)
    - [`MUST_CALL(f)`](#must_callf)
    - [`MUST_CALL(f, count)`](#must_callf-count)
    - [`MUST_NOT_CALL(f)`](#must_not_callf)
    - [`EXPECT_MATCH(text, pattern)`](#expect_matchtext-pattern)
    - [`ASSERT_MATCH(text, pattern)`](#assert_matchtext-pattern)
    - [`EXPECT_NOT_MATCH(text, pattern)`](#expect_not_matchtext-pattern)
    - [`ASSERT_NOT_MATCH(text, pattern)`](#assert_not_matchtext-pattern)
  - [Reporter Extensions](#reporter-extensions)
    - [GTest XML Reporter](#gtest-xml-reporter)
    - [Custom Reporter](#custom-reporter)

## Runner Entry Points

### `MYTEST_CONFIG_USE_MAIN`

Define before including `mytest.h` to generate a default `main()`.

```cpp
#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>
```

Use this in small test binaries that do not need custom process setup.

### `RUN_ALL_TESTS(argc, argv)`

**Returns:** `int` process exit code. Returns `0` when there are no failures,
otherwise `1`.

**Behavior:** runs the selected tests using the command-line options from
`argv`.

```cpp
int main(int argc, char* argv[]) {
  return RUN_ALL_TESTS(argc, argv);
}
```

**Use when:** the binary needs setup before test execution, such as reporter
registration or process-wide configuration.

## Test Names

Tests are registered as `Group:Name`.

```cpp
#include <mytest.h>

TEST(Group, Name) {}
```

The group name is used for hooks and group output. The full name is
`Group:Name`; for example, `TEST(Parser, RejectsInvalidInput)` is registered as
`Parser:RejectsInvalidInput`. The full name is used for filtering and failure
summaries.

## Test Definition Macros

### `TEST(group, name[, timeout_ms])`

Registers a test that normally runs in the main process.

```cpp
TEST(Math, Adds) {
  ASSERT_EQ(2 + 2, 4);
}

TEST(Slow, HasOwnTimeout, 1000) {
  // timeout is 1000 ms
}
```

Without `timeout_ms`, the current default is used. The initial default is
60000 ms. The default for one run can be changed with the command-line option
`-t TIMEOUT`, where `TIMEOUT` is in milliseconds.

Use `TEST` for ordinary checks that can safely share the runner process with
other tests. This is the default choice for pure functions, small integration
tests, and tests that only mutate state they clean up themselves.

### `TEST_ISOLATE(group, name[, timeout_ms])`

Registers a test that runs in a separate process.

```cpp
TEST_ISOLATE(Process, MayCrash) {
  std::abort();
}
```

Output is forwarded to the main process. Failures, skips, timeouts, and signal
termination are reported in the main output.

Use `TEST_ISOLATE` when a test may crash, call `abort()`, or change
process-global state.

## Assertions

### `ASSERT_EQ(x, y)` / `ASSERT_NE(x, y)`

Checks equality or inequality. Stops the current test on failure.

### `EXPECT_EQ(x, y)` / `EXPECT_NE(x, y)`

Checks equality or inequality. Records a failure and continues.

### `ASSERT(cond)` / `EXPECT(cond)`

Boolean checks against `true`.

### Failure Output

Failed assertions print the macro name, source location, original expression,
and evaluated values:

```text
EXPECT_EQ failed (parser-test.cc:42)
  Expected : (result.status == Status::Ok)
    Actual : (Error == Ok)
```

Values are written with `operator<<`. If a type used in `ASSERT_EQ`,
`ASSERT_NE`, `EXPECT_EQ`, or `EXPECT_NE` cannot be streamed to `std::ostream`,
the test will print `<unprintable>`. Add an overload for that type:

```cpp
enum class Status {
  Ok,
  Error,
};

std::ostream& operator<<(std::ostream& os, Status status) {
  switch (status) {
    case Status::Ok:
      return os << "Ok";
    case Status::Error:
      return os << "Error";
  }
  return os << "Unknown";
}
```

For structs, print the fields that matter for debugging:

```cpp
struct Point {
  int x;
  int y;
};

std::ostream& operator<<(std::ostream& os, const Point& point) {
  return os << "Point{x=" << point.x << ", y=" << point.y << "}";
}
```

For project types, place the overload in the same namespace as the type so
argument-dependent lookup can find it:

```cpp
namespace app {

struct UserId {
  int value;
};

std::ostream& operator<<(std::ostream& os, UserId id) {
  return os << "UserId{" << id.value << "}";
}

}  // namespace app
```

## Utility Macros

### `TEST_EXPECT_FAILURE([message])`

Marks the current test as expected to fail. The argument is currently ignored.

Failure becomes success. Success becomes failure.

```cpp
TEST(Negative, RejectsBadInput) {
  TEST_EXPECT_FAILURE();
  ASSERT(Process("bad input"));
}
```

Use this for negative tests where the failure path is the behavior being
verified. For crash or timeout cases, prefer pairing it with `TEST_ISOLATE`.

### `TEST_NAME()`

**Returns:** `const char*` containing the current full test name, such as
`"Group:Name"`.

**Use when:** diagnostic output, generated filenames, or helper code needs to
know which test is running.

```cpp
TEST(Debug, PrintsCurrentName) {
  printf("running %s\n", TEST_NAME());  // running Debug:PrintsCurrentName
}
```

```cpp
TEST(Files, UsesNameInTempFile) {
  std::string name = TEST_NAME();
  std::replace(name.begin(), name.end(), ':', '_');

  auto path = TEST_TEMP_PATH() / (name + ".log");
  std::ofstream(path) << "debug output";
}
```

### `TEST_SKIP([message])`

Skips the current test or hook. A skipped test is reported separately from a
passed or failed test.

```cpp
TEST(Feature, MissingDependency) {
  TEST_SKIP("dependency is not available");
}
```

Use skip for environment-dependent checks, such as a missing external tool or
an unavailable platform feature. Skip is not a substitute for expected failure:
skipped tests are not counted as passed.

```cpp
TEST(Platform, LinuxOnly) {
#ifndef __linux__
  TEST_SKIP("Linux only");
#endif

  ASSERT_EQ(ReadProcStatus(), true);
}
```

The command-line option `-f` enables force mode. In force mode, `TEST_SKIP`
does not stop the test, so execution continues past the skip point. This is
for debugging skipped code paths.

### `TEST_TEMP_PATH()`

**Returns:** `std::filesystem::path` for the current test's temporary
directory. The path is stable within one test body.

**Default path shape:**

```text
<initial-cwd>/.mytest/tmp/<run-directory>/test-<sequence>
```

If the test binary is started from `/repo`, the first temp path created in the
run may look like:

```text
/repo/.mytest/tmp/<run-directory>/test-001
```

Do not depend on the exact run directory name.

The root can be changed with `MyTest::Instance().SetTempRoot(path)`. See
[`MyTest::SetTempRoot(std::filesystem::path)`](#mytestsettemprootstdfilesystempath).

**Behavior:** the directory is created on first use and removed after the test.

**Use when:** a test needs files that should exist only for that test.

## Hooks

Hooks are macros for running setup and cleanup code before, around, and after
tests in the same group. The group is the first argument to `TEST` or
`TEST_ISOLATE`.

### Execution Model

Registration order is preserved for groups and tests.

A normal test is a `TEST` case that was not declared with `TEST_ISOLATE`.

For a normal test in normal mode:

```text
TEST_BEFORE(group)        once before selected normal tests
  TEST_BEFORE_EACH(group)
    TEST(group, name)
  TEST_AFTER_EACH(group)
TEST_AFTER(group)         once after selected normal tests
TEST_AFTER_ALL(group)     final group cleanup
```

For an isolated test:

```text
main process starts isolated test
  TEST_BEFORE(group)
    TEST_BEFORE_EACH(group)
      TEST_ISOLATE(group, name)
    TEST_AFTER_EACH(group)
  TEST_AFTER(group)
main process collects result and cleans temp path
```

The execution model matters most for hooks and global state. Normal tests can
observe mutations made by earlier normal tests. Isolated tests cannot mutate
main-process memory.

### Hook Failures

Hook failures are reported differently depending on where the hook runs:

| Hook | Where failure is reported |
| --- | --- |
| `TEST_BEFORE_EACH` | current test failure; test body may not run |
| `TEST_AFTER_EACH` | current test failure |
| `TEST_BEFORE` for normal tests | group failure; selected tests still run |
| `TEST_AFTER` for normal tests | group failure |
| `TEST_BEFORE` for isolated tests | current test failure |
| `TEST_AFTER` for isolated tests | current test failure |
| `TEST_AFTER_ALL` | group failure |

Example: `TEST_BEFORE_EACH` failure is attached to the current test. The test
body may not run.

```text
[ RUN      ] Hooks
[ RUN      ] Hooks:One
[  FAILED  ] Hooks:One
[  FAILED  ] Hooks
```

Example: `TEST_AFTER_EACH` failure is also attached to the current test, even
when the test body passed.

```text
[ RUN      ] Hooks
[ RUN      ] Hooks:One
[  FAILED  ] Hooks:One
[  FAILED  ] Hooks
```

Example: normal `TEST_BEFORE` failure is attached to the group. The selected
normal tests still run.

```cpp
TEST_BEFORE(Hooks) {
  ASSERT(false);
}

TEST(Hooks, One) {}
TEST(Hooks, Two) {}
```

For normal tests, `TEST_BEFORE` is shared by the selected normal tests in the
group. A failure is attached to the group, and selected normal tests still run.

```text
[ RUN      ] Hooks
[ RUN      ] Hooks:One
[       OK ] Hooks:One
[ RUN      ] Hooks:Two
[       OK ] Hooks:Two
[  FAILED  ] Hooks
```

The same hook fails differently when the tests are isolated.

```cpp
TEST_BEFORE(Hooks) {
  ASSERT(false);
}

TEST_ISOLATE(Hooks, One) {}
TEST_ISOLATE(Hooks, Two) {}
```

For isolated tests, each test runs the group's hook sequence inside its own
isolated execution. A `TEST_BEFORE` failure is attached to that isolated test.

```text
[ RUN      ] Hooks
[ RUN      ] Hooks:One (PID: <pid>)
[  FAILED  ] Hooks:One
[ RUN      ] Hooks:Two (PID: <pid>)
[  FAILED  ] Hooks:Two
[  FAILED  ] Hooks
```

Example: normal `TEST_AFTER` or `TEST_AFTER_ALL` failure is attached to the
group.

```text
[ RUN      ] Hooks
[ RUN      ] Hooks:One
[       OK ] Hooks:One
[  FAILED  ] Hooks
```

```cpp
TEST_BEFORE(Group) {}
TEST_BEFORE_EACH(Group) {}
TEST_AFTER_EACH(Group) {}
TEST_AFTER(Group) {}
TEST_AFTER_ALL(Group) {}
```

### `TEST_BEFORE(group)`

Runs before the group's tests.

If `TEST_BEFORE` skips in the main process, the group is marked skipped and
remaining tests in that group are not run.

Use this for group-level setup. If the setup owns external resources, pair it
with `TEST_AFTER` or `TEST_AFTER_ALL`.

### `TEST_BEFORE_EACH(group)`

Runs before each test in the group.

Use this to reset per-test state.

### `TEST_AFTER_EACH(group)`

Runs after each selected test that reaches cleanup.

Use this for per-test cleanup or per-test assertions.

### `TEST_AFTER(group)`

Runs after the group's tests.

Use this to release resources created by `TEST_BEFORE` for normal tests.
For final work after isolated tests, use `TEST_AFTER_ALL`.

### `TEST_AFTER_ALL(group)`

Runs once in the main process after the group's selected tests and
`TEST_AFTER`.
Useful for cleanup and cross-process verification.

Use this for final cleanup or for verifying state collected from isolated
tests.

#### `TEST_AFTER_ALL` after isolated tests

Use `TEST_AFTER_ALL` for final cleanup or verification after all selected
`TEST_ISOLATE` tests in the group finish. `TEST_AFTER` runs inside each
isolated child as part of that test's hook sequence.

```cpp
TEST_ISOLATE(Files, CrashCase) {
  WriteSharedTrace();
}

TEST_ISOLATE(Files, TimeoutCase) {
  WriteSharedTrace();
}

TEST_AFTER(Files) {
  printf("Files/AFTER\n");
  RemoveSharedTrace();
}
```

```text
[ RUN      ] Files
[ RUN      ] Files:CrashCase (PID: <pid>)
Files/AFTER
[       OK ] Files:CrashCase
[ RUN      ] Files:TimeoutCase (PID: <pid>)
Files/AFTER
[       OK ] Files:TimeoutCase
[       OK ] Files
```

Use `TEST_AFTER_ALL` for final cleanup that must run once in the main process
after those isolated tests.

```cpp
TEST_AFTER_ALL(Files) {
  printf("Files/AFTER_ALL\n");
  RemoveSharedTrace();
}
```

```text
[ RUN      ] Files
[ RUN      ] Files:CrashCase (PID: <pid>)
[       OK ] Files:CrashCase
[ RUN      ] Files:TimeoutCase (PID: <pid>)
[       OK ] Files:TimeoutCase
Files/AFTER_ALL
[       OK ] Files
```

## Timeouts

Default timeout: 60000 ms.

When a test times out, the current test stops and is reported as timed out.
For files, prefer `TEST_TEMP_PATH()` over manual cleanup.

Note: normal `TEST` timeouts skip stack unwinding, local destructors, RAII
cleanup, and lock release.

```cpp
struct Guard {
  ~Guard() { ReleaseLock(); }
};

TEST(Worker, MayHang, 1000) {
  Guard guard;  // ~Guard() is not called on timeout
  RunPossiblyBlockingWork();
}

TEST_ISOLATE(Worker, OwnsProcessState, 1000) {
  RunPossiblyBlockingWork();
}
```

If a test may time out and cleanup matters, consider `TEST_ISOLATE`. The
timed-out child process is discarded, so leaked locks, globals, or
process-local resources do not affect later tests.

## Command-line Options

```text
Usage: <test-binary> [options]
Options:
  -c            : Disable color output
  -f            : Force mode, run all tests, including skipped ones
  -h, --help    : Show the help message
  -j            : Run selected tests separately, one process each
  -l            : List selected tests without running them
  -p "PATTERN"  : Include tests matching PATTERN
  -p "-PATTERN" : Exclude tests matching PATTERN
  -r [FILE]     : Write report via registered reporter
  -s            : Silent mode (suppress stdout and stderr output)
  -t TIMEOUT    : Set the timeout value in milliseconds
```

### Color

Color is enabled by default. `-c` disables ANSI color codes. Isolated tests
inherit the setting.

Use this when output is captured by another test, compared against snapshots,
or consumed by tools that do not handle ANSI escape codes.

### Force Mode

`-f` enables force mode. In force mode, `TEST_SKIP` does not stop execution.
The skipped code path continues as if the skip request had not stopped the
test.

```shell
./test.x -f
```

Use this for local debugging. Avoid it in CI because it changes `TEST_SKIP`
behavior.

### Help

`-h` and `--help` print the command-line option summary and return without
running tests.

### Separate Process Mode

`-j` runs selected tests separately, one process each, including tests declared
with `TEST`. The parent runner schedules selected tests sequentially.

```shell
./test.x -j
```

Use this when selected tests should not share process-global state.

### Filtering

`-p PATTERN` includes tests whose full name matches `PATTERN`. `-p "-PATTERN"`
excludes tests whose full name matches `PATTERN`. Patterns are C++ regular
expressions applied with `std::regex_search` to `Group:Name`.

When no include pattern is provided, all tests are included unless excluded.
Exclude patterns take precedence over include patterns.

The pattern does not need to match the whole test name. Because filtering uses
`std::regex_search`, a plain word matches anywhere in `Group:Name`.

Examples:

```shell
# Run tests whose full name contains Timeout.
./test.x -p "Timeout"

# Run tests whose full name contains Cleanup.
./test.x -p "Cleanup"

# Run every test in the Parser group. ^ means "start of the name".
./test.x -p "^Parser:"

# Run one exact test. $ means "end of the name".
./test.x -p "^Parser:RejectsInvalidInput$"

# Run Parser tests whose test name contains Cleanup.
./test.x -p "^Parser:.*Cleanup"

# Exclude slow tests.
./test.x -p "-Slow"

# Run Parser tests except the slow ones.
./test.x -p "^Parser:" -p "-Slow"

# Include multiple patterns by passing -p more than once.
./test.x -p "Parser" -p "Lexer"

# Run several groups with one regular expression.
./test.x -p "^(Parser|Lexer):"

# Run tests that contain either Timeout or Crash anywhere in the full name.
./test.x -p "(Timeout|Crash)"

# Exclude multiple patterns.
./test.x -p "-Slow" -p "-Flaky"
```

### Reports

`-r` requires a registered reporter. Without one, the runner prints
`No reporter registered.` and exits with code `1`.

With no file argument, `-r` passes an empty output path. The built-in
`GTestXmlReporter` maps that to `test_report.xml`.

Use reports for CI systems or tools that collect test results from files
instead of console output.

See [Reporter Extensions](#reporter-extensions) for registering a reporter and
writing a custom output format.

### Silent Mode

`-s` redirects test-body stdout and stderr to `/dev/null`. Runner status output
remains visible. Isolated tests inherit the setting.

Use this for snapshot-style tests or noisy integration tests where runner
status is enough.

### Timeout Option

`-t TIMEOUT` changes the default timeout for this run. `TIMEOUT` is in
milliseconds. A test-specific timeout passed to `TEST` or `TEST_ISOLATE` still
takes precedence for that test.

```shell
./test.x -t 1000
```

Use this to make a whole test binary stricter or looser without editing test
source.

## Test Runner API

Use these helpers to configure the test runner or read runner state.

### `MyTest::Instance()`

**Returns:** reference to the active runner.

**Use when:** a helper needs to configure the runner.

```cpp
void UseCustomTempRoot(const std::filesystem::path& root) {
  MyTest::Instance().SetTempRoot(root);
}
```

### `MyTest::IsMainProcess()`

**Returns:** `true` in the main process, `false` in isolated tests.

**Use when:** a hook should only change state owned by the main process.

```cpp
TEST_BEFORE(Cache) {
  if (!MyTest::IsMainProcess()) {
    TEST_SKIP();
  }
  BuildSharedCache();
}
```

### `MyTest::SetTempRoot(std::filesystem::path)`

**Returns:** none.

**Behavior:** overrides the root used by `TEST_TEMP_PATH()`. Passing an empty
path resets to the initial current working directory.

If the root is `.tmp-for-files`, temp paths are created under:

```text
.tmp-for-files/.mytest/tmp/<run-directory>/test-<sequence>
```

**Use when:** tests must place temporary files under a specific filesystem, such
as a ramdisk, sandbox directory, or fixture-controlled directory.

```cpp
TEST(Files, UsesCustomTempRoot) {
  MyTest::Instance().SetTempRoot(".tmp-for-files");
  auto path = TEST_TEMP_PATH();
  ASSERT(std::filesystem::exists(path));
  MyTest::Instance().SetTempRoot({});
}
```

### `MyTest::SilenceScope`

**Behavior:** suppresses stdout and stderr while the scope is alive. Nested
scopes are supported.

**Use when:** only a small part of a test is noisy. The command-line option
`-s` enables silent mode for the whole run and hides stdout/stderr from all test
bodies; `SilenceScope` is the scoped version.

```cpp
TEST(Output, NoisyCall) {
  printf("visible before\n");
  {
    MyTest::SilenceScope silence;
    RunNoisyCode();
  }
  printf("visible after\n");
}
```

## Output and Failure Summary

Grouped googletest-style status:

```text
[ RUN      ] Group
[ RUN      ] Group:Name
[       OK ] Group:Name
[       OK ] Group
```

Failures are summarized at the end. Assertion details are listed under
`Group:Name`.

Signal exits:

```text
Terminated by signal <number> (<name>)
```

## Extensions

Extensions add optional behavior outside the core `mytest.h` header. There are
two main kinds:

- test helper extensions add assertion-like helpers
- reporter extensions export finished test results

### Test Helper Extensions

Optional test helpers can live in separate headers. Use this for
assertion-like features outside the core header.

The
[test/03-advanced/ext/mytest-must-call.h](test/03-advanced/ext/mytest-must-call.h)
helper adds callback call-count checks.

#### `MUST_CALL(f)`

Expects a callable to run once.

#### `MUST_CALL(f, count)`

Expects a callable to run exactly `count` times.

#### `MUST_NOT_CALL(f)`

Expects a callable not to run.

Example:

```cpp
TEST(CallVerification, CalledOnce) {
  auto cb = MUST_CALL([]() {});
  cb();
}
```

The
[test/03-advanced/ext/mytest-match.h](test/03-advanced/ext/mytest-match.h)
helper adds regular-expression checks for text.

#### `EXPECT_MATCH(text, pattern)`

Records a failure if `text` does not match `pattern`.

#### `ASSERT_MATCH(text, pattern)`

Stops the current test if `text` does not match `pattern`.

#### `EXPECT_NOT_MATCH(text, pattern)`

Records a failure if `text` matches `pattern`.

#### `ASSERT_NOT_MATCH(text, pattern)`

Stops the current test if `text` matches `pattern`.

Example:

```cpp
TEST(Match, ContainsGeneratedId) {
  EXPECT_MATCH("user-42", "user-[0-9]+");
}
```

See [test/03-advanced/ext/README.md](test/03-advanced/ext/README.md)
for usage details.

### Reporter Extensions

Reporters export completed test results to a file or another external format.
They are not used for normal console output. A reporter is registered in
`main()`, then activated at runtime with `-r [FILE]`.

#### GTest XML Reporter

Include `mytest-report.h` to use the bundled reporter extension:

```cpp
#include <mytest-report.h>
```

The extension provides `mytest::GTestXmlReporter`.

Register it before calling `RUN_ALL_TESTS`, then request report output with
`-r`. Without a file argument, the report path is `test_report.xml`.

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
./test.x -r custom.xml
```

The reporter writes a `<testsuites>` document. If the target file already uses
the same format, existing cases are loaded and new results are appended.

Use this extension when a CI system already understands googletest-style XML.

#### Custom Reporter

Implement `mytest::Reporter` when the built-in XML reporter is not the format
you need. The runner calls `OnComplete()` once after all selected tests finish.

`OnComplete()` receives:

- `results`: one `TestResult` per completed test case
- `summary`: total, failed, and skipped counts
- `options`: command-line report options, including the optional output path

The main report types are:

```cpp
namespace mytest {
struct TestResult {
  std::string suite;
  std::string name;
  bool failure = false;
  bool skipped = false;
  std::string message;
  std::vector<std::string> details;
};

struct Summary {
  int total = 0;
  int failures = 0;
  int skipped = 0;
};

struct Options {
  std::string output_path;
};

class Reporter {
 public:
  virtual ~Reporter() = default;
  virtual void OnComplete(const std::vector<TestResult>& results,
                          const Summary& summary,
                          const Options& options) = 0;
};
}
```

Example: write a simple text report.

```cpp
#include <fstream>
#include <mytest.h>

class TextReporter : public mytest::Reporter {
 public:
  void OnComplete(const std::vector<mytest::TestResult>& results,
                  const mytest::Summary& summary,
                  const mytest::Options& options) override {
    const std::string path =
        options.output_path.empty() ? "test_report.txt" : options.output_path;

    std::ofstream out(path);
    out << "total: " << summary.total << "\n";
    out << "failed: " << summary.failures << "\n";
    out << "skipped: " << summary.skipped << "\n\n";

    for (const auto& result : results) {
      out << result.suite << ":" << result.name << " ";
      if (result.skipped) {
        out << "SKIPPED";
      } else if (result.failure) {
        out << "FAILED";
      } else {
        out << "OK";
      }

      if (!result.message.empty()) {
        out << " - " << result.message;
      }
      out << "\n";

      for (const auto& detail : result.details) {
        out << "  " << detail << "\n";
      }
    }
  }
};

int main(int argc, char* argv[]) {
  SET_REPORTER(TextReporter);
  return RUN_ALL_TESTS(argc, argv);
}
```

`OnComplete()` is called only when report output is requested with `-r [FILE]`.

```shell
./test.x -r
./test.x -r result.txt
```
