/*
 * 01-basics/test1.cc
 *
 * This file demonstrates:
 * - Basic test macros: TEST, TEST_ASYNC, TEST0
 * - Timeout handling with millisecond precision
 * - TEST_SKIP() and TEST_EXPECT_FAILURE() usage
 * - All lifecycle hooks: BEFORE, AFTER, BEFORE_EACH, AFTER_EACH
 * - Thread isolation validation (tests run on separate threads)
 * - Local fixture pattern for sharing state between tests
 */

#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>
#include <future>
#include <thread>
#include "../fixture.h"

Fixture f;

TEST(TestSuite1, SyncTest) {
  EXPECT_EQ(1, f.before);
  EXPECT_EQ(0, f.after);
  f.count++;
  ASSERT_EQ(f.main_thread_id, std::this_thread::get_id());
}

TEST(TestSuite1, SyncTestFailure) {
  TEST_EXPECT_FAILURE();
  EXPECT_EQ(1, 0);
  f.expect++;
  ASSERT_EQ(1, 0);
  f.expect++;
  f.count++;
}

TEST(TestSuite1, SyncTestTimeout, 500) {
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  f.count++;
}

TEST(TestSuite1, SyncTestOnCurrentThread) {
  f.count++;
  ASSERT_EQ(f.main_thread_id, std::this_thread::get_id());
}

TEST(TestSuite1, SyncTestSkip) {
  f.skip++;
  TEST_SKIP();
  f.count++;
}

TEST(TestSuite1, ASyncTest) {
  std::future<void> async_future = std::async(std::launch::async, []() {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    f.count++;
  });
  async_future.get();
}

TEST(TestSuite1, ASyncTestTimeout, 500) {
  TEST_EXPECT_FAILURE();
  std::future<void> async_future = std::async(std::launch::async, []() {
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    f.count++;
  });
  async_future.get();
}

TEST(TestSuite1, ASyncTestSkip) {
  TEST_SKIP();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  f.count++;
}

TEST_BEFORE_EACH(TestSuite1) {
  std::cout << "Before: each TestSuite1 test" << std::endl;
  f.before_each++;
}

TEST_AFTER_EACH(TestSuite1) {
  std::cout << "After : each TestSuite1 test" << std::endl;
  f.after_each++;
}

TEST_BEFORE(TestSuite1) {
  if (!IS_MAIN_PROCESS()) { TEST_SKIP(); }
  std::cout << "\nRuns  : once before all TestSuite1 tests" << std::endl;
  f.before++;
  f.main_thread_id = std::this_thread::get_id();
}

TEST_AFTER(TestSuite1) {
  std::cout << "Runs  : once after all TestSuite1 tests\n" << std::endl;
  f.after++;

  EXPECT_EQ(f.before, 1);
  EXPECT_EQ(f.after, 1);
  EXPECT_EQ(f.before_each, 8);
  EXPECT_EQ(f.after_each, 8);
  EXPECT_EQ(f.skip, 1);
  EXPECT_EQ(f.expect, 1);
  EXPECT_EQ(f.count, 3);
}
