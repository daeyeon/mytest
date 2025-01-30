#include <mytest.h>

int global;

TEST(Subject, SyncTest) {
  ASSERT_EQ(1, global);
}

TEST(Subject, SyncTestTimeout, 1000) {
  std::this_thread::sleep_for(std::chrono::seconds(2));
  ASSERT_EQ(1, global);
}

TEST0(Subject, SyncTestOnCurrentThread) {
  // Runs on current thread; others on separate threads until timeout.
  ASSERT_EQ(1, global);
}

TEST(Subject, SyncTestSkip) {
  TEST_SKIP("Skipping this test");
  ASSERT_EQ(1, global);
}

TEST_ASYNC(Subject, ASyncTest) {
  std::this_thread::sleep_for(std::chrono::seconds(1));
  ASSERT_EQ(1, global);
  done();
}

TEST_ASYNC(Subject, ASyncTestTimeout, 1000) {
  std::this_thread::sleep_for(std::chrono::seconds(2));
  ASSERT_EQ(1, global);
  done();
}

TEST_ASYNC(Subject, ASyncTestSkip) {
  TEST_SKIP("Skipping this test");
  std::this_thread::sleep_for(std::chrono::seconds(1));
  ASSERT_EQ(1, global);
  done();
}

TEST_BEFORE_EACH(Subject) {
  std::cout << "Before each Subject test" << std::endl;
}

TEST_AFTER_EACH(Subject) {
  std::cout << "After each Subject test" << std::endl;
}

TEST_BEFORE(Subject) {
  std::cout << "Runs once before all Subject tests" << std::endl;
}

TEST_AFTER(Subject) {
  std::cout << "Runs once after all Subject tests" << std::endl;
}

int main(int argc, char* argv[]) {
  global = 1;
  return RUN_ALL_TESTS(argc, argv);
}
