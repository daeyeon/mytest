#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>
#include <array>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>
#include "../test-helpers.h"

static int global_counter = 0;
static bool global_flag = false;

TEST_BEFORE(Basic) {
  printf("%s/BEFORE\n", TEST_NAME());
  EXPECT_EQ(0, global_counter);
  EXPECT_EQ(false, global_flag);
}

TEST_BEFORE_EACH(Basic) {
  printf("%s/BEFORE_EACH\n", TEST_NAME());
  global_counter = 0;
  global_flag = false;
}

TEST_AFTER_EACH(Basic) { printf("%s/AFTER_EACH\n", TEST_NAME()); }

TEST(Basic, 1) {
  printf("%s/TEST\n", TEST_NAME());
  global_counter = 100;
  global_flag = true;

  EXPECT_EQ(100, global_counter);
  EXPECT_EQ(true, global_flag);
}

TEST(Basic, 2) {
  printf("%s/TEST\n", TEST_NAME());
  TEST_SKIP();
}

TEST(Basic, 3) {
  printf("%s/TEST\n", TEST_NAME());
  TEST_EXPECT_FAILURE();
  ASSERT_EQ(true, global_flag);
}

TEST(Basic, 4) {
  printf("%s/TEST\n", TEST_NAME());
  TEST_EXPECT_FAILURE();
  EXPECT_EQ(100, global_counter);
  global_counter = 100;
  global_flag = true;
}

TEST(Basic, TimeoutTest, 500) {
  printf("%s/TEST\n", TEST_NAME());
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  global_counter = 12345;
  global_flag = true;
  EXPECT_EQ(12345, global_counter);
}

TEST_AFTER(Basic) {
  printf("%s/AFTER\n", TEST_NAME());
  EXPECT_NE(12345, global_counter);
  EXPECT_NE(true, global_flag);
}

TEST(TestOutput, SnapshotBasic) {
  printf("%s/TEST\n", TEST_NAME());
  bool result = VerifySnapshotOutput({"-p", "^Basic", "-c"}, "test/01-basics/basic.out");
  EXPECT_EQ(true, result);
}
