#include <mytest.h>
#include "../test-helpers.h"
#include <fstream>
#include <sstream>

static int global_counter = 0;
static bool global_flag = false;

TEST_BEFORE(Isolation) {
  printf("%s/BEFORE\n", TEST_NAME());
  EXPECT_EQ(0, global_counter);
  EXPECT_EQ(false, global_flag);
}

TEST_AFTER(Isolation) {
  printf("%s/AFTER\n", TEST_NAME());
  EXPECT_NE(0, global_counter);
  EXPECT_NE(false, global_flag);
}

TEST_BEFORE_EACH(Isolation) { printf("%s/BEFORE_EACH\n", TEST_NAME()); }

TEST_AFTER_EACH(Isolation) { printf("%s/AFTER_EACH\n", TEST_NAME()); }

TEST_ISOLATE(Isolation, 1) {
  printf("%s/TEST\n", TEST_NAME());
  global_counter = 100;
  global_flag = true;

  EXPECT_EQ(100, global_counter);
  EXPECT_EQ(true, global_flag);
}

TEST_ISOLATE(Isolation, 2) {
  printf("%s/TEST\n", TEST_NAME());
  global_counter = 100;
  global_flag = true;
  TEST_SKIP();
}

TEST_ISOLATE(Isolation, 3) {
  printf("%s/TEST\n", TEST_NAME());
  TEST_EXPECT_FAILURE();
  ASSERT_EQ(true, global_flag);
}

TEST_ISOLATE(Isolation, 4) {
  printf("%s/TEST\n", TEST_NAME());
  TEST_EXPECT_FAILURE();
  EXPECT_EQ(100, global_counter);
  global_counter = 100;
  global_flag = true;
}

TEST_ISOLATE(Isolation, TimeoutTest, 500) {
  printf("%s/TEST\n", TEST_NAME());
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  global_counter = 12345;
  EXPECT_EQ(12345, global_counter);
}

TEST(TestOutput, SnapshotBasic1) {
  printf("%s/TEST\n", TEST_NAME());
  bool result = VerifySnapshotOutput(
      {"-p", "^Isolation", "-c"},
      "test/01-basics/basic1.out");
  EXPECT_EQ(true, result);
}
