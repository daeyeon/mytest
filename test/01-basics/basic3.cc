#include <mytest.h>
#include "../test_helpers.h"

static int global;
static int global_counter = 0;

TEST_ISOLATE(Process, CwdIsolationVerification) {
  char buf[4096];
  if (getcwd(buf, sizeof(buf))) {
    std::string cwd(buf);
    ASSERT_EQ(cwd.find("/tmp"), std::string::npos);
  }
}

TEST_ISOLATE(Process, IncrementCounterInChild) {
  ++global_counter;
  ASSERT_EQ(global_counter, 1);
}

TEST_ISOLATE(Process, SkipInChild) {
  TEST_SKIP("Skipping in isolated process");
  ASSERT_EQ(0, 1);
}

TEST_ISOLATE(Process, TimeoutInChild, 500) {
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  ASSERT_EQ(1, global);
}

TEST_ISOLATE(Process, CounterIsolation) { ASSERT_EQ(global_counter, 0); }

TEST(TestOutput, SnapshotBasic3) {
  printf("%s/TEST\n", TEST_NAME());
  bool result = VerifySnapshotOutput(
      {"-p", "^Process", "-c"},
      "test/01-basics/basic3.out");
  EXPECT_EQ(true, result);
}
