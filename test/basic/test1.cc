#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>
#include <future>
#include <thread>
#include "fixture.h"

Fixture f;

TEST(TestSuite1, SyncTest) {
  EXPECT_EQ(1, f.before);
  EXPECT_EQ(0, f.after);
  f.count++;
  ASSERT_NE(f.main_thread_id, std::this_thread::get_id());
}

TEST(TestSuite1, SyncTestFailure) {
  TEST_EXPECT_FAILURE();
  EXPECT_EQ(1, 0);
  f.expect++;
  ASSERT_EQ(1, 0);
  f.expect++;
  f.count++;
}

TEST(TestSuite1, SyncTestTimeout, 1000) {
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  f.count++;
}

TEST0(TestSuite1, SyncTestOnCurrentThread) {
  f.count++;
  ASSERT_EQ(f.main_thread_id, std::this_thread::get_id());
}

TEST(TestSuite1, SyncTestSkip) {
  f.skip++;
  TEST_SKIP();
  f.count++;
}

TEST_ASYNC(TestSuite1, ASyncTest) {
  auto this_thread_id = std::this_thread::get_id();
  ASSERT_NE(f.main_thread_id, std::this_thread::get_id());
  std::async(std::launch::async, [&done, &this_thread_id]() {
    ASSERT_NE(this_thread_id, std::this_thread::get_id());
    std::this_thread::sleep_for(std::chrono::seconds(1));
    f.count++;
    done();
  }).get();
}

TEST_ASYNC(TestSuite1, ASyncTestTimeout, 1000) {
  TEST_EXPECT_FAILURE();
  std::async(std::launch::async, [&done]() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    f.count++;
    done();
  }).get();
}

TEST_ASYNC(TestSuite1, ASyncTestSkip) {
  TEST_SKIP();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  f.count++;
  done();
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
  if (MyTest::Instance().IsJobIsolated()) { TEST_SKIP(); }
  std::cout << "\nRuns  : once before all TestSuite1 tests" << std::endl;
  f.before++;
  f.main_thread_id = std::this_thread::get_id();
}

TEST_AFTER(TestSuite1) {
  std::cout << "Runs  : once after all TestSuite1 tests\n" << std::endl;
  f.after++;

  if (MyTest::Instance().IsJobIsolated()) return;

  EXPECT_EQ(f.before, 1);
  EXPECT_EQ(f.after, 1);
  EXPECT_EQ(f.before_each, 8);
  EXPECT_EQ(f.after_each, 8);
  EXPECT_EQ(f.skip, 1);
  EXPECT_EQ(f.expect, 1);
  EXPECT_EQ(f.count, 4);
}
