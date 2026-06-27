# Test Helper Extensions

This directory contains optional helper examples built on top of `mytest.h`.

These helpers are not part of the core runner. Keep optional test-only features
here instead of growing the main header.

## `mytest-must-call.h`

`mytest-must-call.h` adds callback call-count checks.

Include it from a test file:

```cpp
#include <mytest.h>
#include "plugins/mytest-must-call.h"
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

## Async callbacks

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

## Failure timing

The helper reports call-count failures after the test body finishes.

```cpp
TEST(CallVerification, MissingCall) {
  TEST_EXPECT_FAILURE();

  auto cb = MUST_CALL([]() {});
  (void)cb;
}
```

This fails because `cb` was expected once but called zero times.
