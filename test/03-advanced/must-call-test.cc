#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>
#include <future>
#include <thread>
#include "ext/mytest-must-call.h"

TEST(CallVerification, MustCallSuccess) {
  auto cb = MUST_CALL([](int x) { EXPECT_EQ(x, 42); });
  cb(42);
}

TEST(CallVerification, MustCallExactCount) {
  auto cb = MUST_CALL([]() {}, 3);
  cb();
  cb();
  cb();
}

TEST(CallVerification, MustNotCallSuccess) {
  auto cb = MUST_NOT_CALL([]() {});
  (void)cb;  // Unused
}

TEST(CallVerification, MustCallFailure) {
  TEST_EXPECT_FAILURE();
  auto cb = MUST_CALL([]() {});
  // cb is NOT called, should fail at end of test.
  (void)cb;
}

TEST(CallVerification, MustCallTooMany) {
  TEST_EXPECT_FAILURE();
  auto cb = MUST_CALL([]() {}, 1);
  cb();
  cb();  // Called twice, expected once.
}

TEST(CallVerification, MustNotCallFailure) {
  TEST_EXPECT_FAILURE();
  auto cb = MUST_NOT_CALL([]() {});
  cb();  // Called but expected 0.
}

TEST_ISOLATE(CallVerification, IsolatedMustCallSuccess) {
  auto cb = MUST_CALL([]() {});
  cb();
}

TEST_ISOLATE(CallVerification, IsolatedMustCallFailure) {
  TEST_EXPECT_FAILURE();
  auto cb = MUST_CALL([]() {});
  // cb is NOT called
}

TEST_ISOLATE(CallVerification, IsolatedMustNotCallFailure) {
  TEST_EXPECT_FAILURE();
  auto cb = MUST_NOT_CALL([]() {});
  cb();
}

std::future<void> FetchDataAsync(std::function<void(const std::string&)> callback, int delay_ms) {
  return std::async(std::launch::async, [callback, delay_ms]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    callback("Success");
  });
}

TEST(CallVerification, RealWorldAsyncCallback) {
  auto on_complete = MUST_CALL([](const std::string& res) { EXPECT_EQ(res, "Success"); });

  FetchDataAsync(on_complete, 20).wait();
}

TEST(CallVerification, RealWorldAsyncCallbackTimeout, 1000) {
  TEST_EXPECT_FAILURE();
  auto on_complete = MUST_CALL([](const std::string& res) { EXPECT_EQ(res, "Success"); });
  FetchDataAsync(on_complete, 2000).wait();
}

struct OrderService {
  std::function<void()> notify_logic;
  void PlaceOrder() { notify_logic(); }
};

TEST(CallVerification, DependencyInjectionExample) {
  OrderService service;
  service.notify_logic = MUST_CALL([]() { printf("Order placed notification sent.\n"); });

  FetchDataAsync([&](const std::string&) { service.PlaceOrder(); }, 1000).wait();
}
