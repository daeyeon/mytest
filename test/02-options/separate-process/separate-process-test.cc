#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>
#include <future>
#include <thread>
#include "../../fixture.h"
#include "../../shared-memory.h"
#include "../../test-helpers.h"

// Single fixture for all tests in separate-process mode.
static constexpr const char* FIXTURE_NAME = "/separate_process_fixture";

/*--- Basic Tests ---*/
TEST(SeparateProcessTests, BasicSync) {
  printf("%s/TEST\n", TEST_NAME());
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    f.count++;
  });
}

TEST(SeparateProcessTests, BasicSyncWithFailure) {
  printf("%s/TEST\n", TEST_NAME());
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    TEST_EXPECT_FAILURE();
    EXPECT_EQ(1, 0);
    f.expect++;
    ASSERT_EQ(1, 0);
    f.expect++;
    f.count++;
  });
}

TEST(SeparateProcessTests, BasicTimeout, 500) {
  printf("%s/TEST\n", TEST_NAME());
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
}

TEST(SeparateProcessTests, BasicSkip) {
  printf("%s/TEST\n", TEST_NAME());
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    f.skip++;
    TEST_SKIP();
  });
}

TEST(SeparateProcessTests, AsyncTest) {
  printf("%s/TEST\n", TEST_NAME());
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    std::future<void> async_future = std::async(std::launch::async, [&f]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      f.count++;
    });
    async_future.get();
  });
}

/*--- Hooks: Per-Test ---*/
TEST_BEFORE_EACH(SeparateProcessTests) {
  printf("SeparateProcessTests/BEFORE_EACH\n");
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    f.before_each++;
  });
}

TEST_AFTER_EACH(SeparateProcessTests) {
  printf("SeparateProcessTests/AFTER_EACH\n");
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    f.after_each++;
  });
}

/*--- Hooks: Suite-Level ---*/
TEST_BEFORE(SeparateProcessTests) {
  printf("SeparateProcessTests/BEFORE\n");
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    f.before++;
    f.main_thread_id = std::this_thread::get_id();
  });
}

TEST_AFTER(SeparateProcessTests) {
  printf("SeparateProcessTests/AFTER\n");
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    // Clean up (no verification needed, hooks are basic validation in 01-basics)
    ShmRegion<Fixture>::OpenOrCreate(FIXTURE_NAME).Remove();
  });
}

TEST(SeparateProcessSnapshot, VerifySeparateProcessOutput) {
  if (getenv("IS_SPAWNED_CHILD") != nullptr) {
    TEST_SKIP("Already in spawned child process - prevent recursive execution");
  }

  printf("%s/TEST\n", TEST_NAME());
  bool result = VerifySnapshotOutput(
      {"-p", "^SeparateProcessTests", "-j", "-c"},
      "test/02-options/separate-process/separate-process-test.out");
  EXPECT_EQ(true, result);
}
