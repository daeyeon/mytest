#define MYTEST_CONFIG_USE_MAIN
#include "mytest.h"

int global;

TEST(TestSuite, SyncTest) {
  ASSERT_EQ(1, global);
}

TEST(TestSuite, SyncTestTimeout, 1000) {
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  ASSERT_EQ(1, global);
}

TEST0(TestSuite, SyncTestOnCurrentThread) {  // No timeout support in TEST0.
  // Runs on the thread executing the test; other tests run on separate ones.
  ASSERT_EQ(1, global);
}

TEST(TestSuite, SyncTestSkip) {
  TEST_SKIP();
  ASSERT_EQ(1, global);
}

TEST_ASYNC(TestSuite, ASyncTest) {
  std::async(std::launch::async, [&done]() {
    ASSERT_EQ(1, global);
    done();  // Call `done()` passed as a parameter when async completes.
  }).get();
}

TEST_ASYNC(TestSuite, ASyncTestTimeout, 1000) {
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  ASSERT_EQ(1, global);
  done();
}

TEST_ASYNC(TestSuite, ASyncTestSkip) {
  TEST_SKIP();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  ASSERT_EQ(1, global);
  done();
}

TEST_BEFORE_EACH(TestSuite) {
  std::cout << "Before each TestSuite test" << std::endl;
}

TEST_AFTER_EACH(TestSuite) {
  std::cout << "After each TestSuite test" << std::endl;
}

TEST_BEFORE(TestSuite) {
  std::cout << "Runs once before all TestSuite tests" << std::endl;
  global = 1;
}

TEST_AFTER(TestSuite) {
  std::cout << "Runs once after all TestSuite tests" << std::endl;
}

TEST(TestSuite, ExcludeTest) {
  ASSERT_EQ(1, 0);
}

TEST(TestSuite2, ExcludeTest) {
  ASSERT_EQ(1, 0);
}

TEST_EXCLUDE(TestSuite, ExcludeTest)

TEST_EXCLUDE(TestSuite2)
