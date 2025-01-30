#include <mytest.h>

int global;

TEST(TestSuite, SyncTest) {
  ASSERT_EQ(1, global);
}

TEST(TestSuite, SyncTestTimeout, 1000) {
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  ASSERT_EQ(1, global);
}

TEST0(TestSuite, SyncTestOnCurrentThread) {
  // Runs on current thread; others on separate threads until timeout.
  ASSERT_EQ(1, global);
}

TEST(TestSuite, SyncTestSkip) {
  TEST_SKIP("Skipping this test");
  ASSERT_EQ(1, global);
}

TEST_ASYNC(TestSuite, ASyncTest) {
  std::this_thread::sleep_for(std::chrono::seconds(1));
  ASSERT_EQ(1, global);
  done();
}

TEST_ASYNC(TestSuite, ASyncTestTimeout, 1000) {
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  ASSERT_EQ(1, global);
  done();
}

TEST_ASYNC(TestSuite, ASyncTestSkip) {
  TEST_SKIP("Skipping this test");
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
}

TEST_AFTER(TestSuite) {
  std::cout << "Runs once after all TestSuite tests" << std::endl;
}

int main(int argc, char* argv[]) {
  global = 1;
  return RUN_ALL_TESTS(argc, argv);
}
