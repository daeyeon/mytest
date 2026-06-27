# Test Helper Extensions

This directory contains optional helper examples built on top of `mytest.h`.

These helpers are not part of the core runner. Keep optional test-only features
here instead of growing the main header.

## Contents

- [`mytest-must-call.h`](#mytest-must-callh)
  - [`MUST_CALL(f)`](#must_callf)
  - [`MUST_CALL(f, count)`](#must_callf-count)
  - [`MUST_NOT_CALL(f)`](#must_not_callf)
  - [Async callbacks](#async-callbacks)
  - [Failure timing](#failure-timing)
- [`mytest-match.h`](#mytest-matchh)
  - [`EXPECT_MATCH(text, pattern)`](#expect_matchtext-pattern)
  - [`ASSERT_MATCH(text, pattern)`](#assert_matchtext-pattern)
  - [`EXPECT_NOT_MATCH(text, pattern)`](#expect_not_matchtext-pattern)
  - [`ASSERT_NOT_MATCH(text, pattern)`](#assert_not_matchtext-pattern)

## `mytest-must-call.h`

`mytest-must-call.h` adds callback call-count checks.

Include it from a test file:

```cpp
#include <mytest.h>
#include "mytest-must-call.h"
```

### `MUST_CALL(f)`

Wraps a callable and expects it to be called exactly once.

```cpp
TEST(CallVerification, CalledOnce) {
  auto cb = MUST_CALL([](int value) {
    EXPECT_EQ(value, 42);
  });

  cb(42);
}
```

If `cb` is not called, the test fails after the test body finishes.

### `MUST_CALL(f, count)`

Wraps a callable and expects it to be called exactly `count` times.

```cpp
TEST(CallVerification, CalledThreeTimes) {
  auto cb = MUST_CALL([]() {}, 3);

  cb();
  cb();
  cb();
}
```

Calling it fewer or more times fails the test.

### `MUST_NOT_CALL(f)`

Wraps a callable and expects it not to be called.

```cpp
TEST(CallVerification, NotCalled) {
  auto cb = MUST_NOT_CALL([]() {});

  (void)cb;
}
```

Calling `cb()` fails the test after the test body finishes.

### Async callbacks

`MUST_CALL` is useful when an async API reports completion through a callback.

```cpp
TEST(CallVerification, AsyncCallback) {
  auto on_complete = MUST_CALL([](const std::string& result) {
    EXPECT_EQ(result, "Success");
  });

  FetchDataAsync(on_complete).wait();
}
```

Make sure the async work has completed before the test body exits. The call
count is checked after the test body finishes.

### Failure timing

The helper reports call-count failures after the test body finishes.

```cpp
TEST(CallVerification, MissingCall) {
  TEST_EXPECT_FAILURE();

  auto cb = MUST_CALL([]() {});
  (void)cb;
}
```

This fails because `cb` was expected once but called zero times.

## `mytest-match.h`

`mytest-match.h` adds regular-expression checks for text.

Include it from a test file:

```cpp
#include <mytest.h>
#include "mytest-match.h"
```

### `EXPECT_MATCH(text, pattern)`

Records a failure if `text` does not match `pattern`.

```cpp
TEST(Match, ContainsGeneratedId) {
  EXPECT_MATCH("user-42", "user-[0-9]+");
}
```

### `ASSERT_MATCH(text, pattern)`

Stops the current test if `text` does not match `pattern`.

```cpp
TEST(Match, RequiresStatus) {
  ASSERT_MATCH("status: ok", "status: (ok|ready)");
}
```

### `EXPECT_NOT_MATCH(text, pattern)`

Records a failure if `text` matches `pattern`.

```cpp
TEST(Match, NoErrorCode) {
  EXPECT_NOT_MATCH("status: ok", "error-[0-9]+");
}
```

### `ASSERT_NOT_MATCH(text, pattern)`

Stops the current test if `text` matches `pattern`.

```cpp
TEST(Match, RejectsDebugBuildId) {
  ASSERT_NOT_MATCH("build: release-2026", "debug-[0-9]+");
}
```

Patterns are C++ regular expressions. Invalid patterns fail the check.
