#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>
#include <future>
#include <thread>
#include "../../fixture.h"
#include "../../shared_memory.h"
#include "../../test_helpers.h"

// Single fixture for all tests in job mode
static constexpr const char* FIXTURE_NAME = "/job_mode_fixture";

/*--- Basic Tests ---*/
TEST(JobModeTests, BasicSync) {
  printf("%s/TEST\n", TEST_NAME());
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    f.count++;
  });
}

TEST(JobModeTests, BasicSyncWithFailure) {
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

TEST(JobModeTests, BasicTimeout, 500) {
  printf("%s/TEST\n", TEST_NAME());
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
}

TEST(JobModeTests, BasicSkip) {
  printf("%s/TEST\n", TEST_NAME());
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    f.skip++;
    TEST_SKIP();
  });
}

TEST(JobModeTests, AsyncTest) {
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
TEST_BEFORE_EACH(JobModeTests) {
  printf("JobModeTests/BEFORE_EACH\n");
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    f.before_each++;
  });
}

TEST_AFTER_EACH(JobModeTests) {
  printf("JobModeTests/AFTER_EACH\n");
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    f.after_each++;
  });
}

/*--- Hooks: Suite-Level ---*/
TEST_BEFORE(JobModeTests) {
  printf("JobModeTests/BEFORE\n");
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    f.before++;
    f.main_thread_id = std::this_thread::get_id();
  });
}

TEST_AFTER(JobModeTests) {
  printf("JobModeTests/AFTER\n");
  WithShmMemory<Fixture>(FIXTURE_NAME, [](auto& f) {
    // Clean up (no verification needed, hooks are basic validation in 01-basics)
    ShmRegion<Fixture>::OpenOrCreate(FIXTURE_NAME).Remove();
  });
}

TEST(JobModeSnapshot, VerifyJobModeOutput) {
  if (getenv("IS_SPAWNED_CHILD") != nullptr) {
    TEST_SKIP("Already in spawned child process - prevent recursive execution");
  }

  printf("%s/TEST\n", TEST_NAME());
  bool result = VerifySnapshotOutput(
      {"-p", "^JobModeTests", "-j", "-c"},
      "test/02-options/job-mode/job_mode_test.out");
  EXPECT_EQ(true, result);
}
