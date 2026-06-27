#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>
#include "../test-helpers.h"

static bool IsChildRun() { return getenv("IS_SPAWNED_CHILD") != nullptr; }

static void ExpectChildFailure(const char* pattern) {
  RunResult result = ExecuteSelfResult({"-p", pattern, "-c", "-s", nullptr});
  EXPECT_EQ(result.exit_status, 1);
}

static void FailHook() {
  bool hook_should_fail = true;
  EXPECT_EQ(hook_should_fail, false);
}

TEST(HookFailureExit, ReportsHookFailures) {
  if (IsChildRun()) {
    TEST_SKIP("In child run");
  }

  // Each child run selects one hook-failure group.
  ExpectChildFailure("^BeforeFailure:");
  ExpectChildFailure("^BeforeEachFailure:");
  ExpectChildFailure("^AfterEachFailure:");
  ExpectChildFailure("^AfterFailure:");
  ExpectChildFailure("^AfterAllFailure:");
}

TEST_BEFORE(BeforeFailure) {
  if (IsChildRun()) FailHook();
}

TEST(BeforeFailure, Body) {}

TEST_BEFORE_EACH(BeforeEachFailure) {
  if (IsChildRun()) FailHook();
}

TEST(BeforeEachFailure, Body) {}

TEST_AFTER_EACH(AfterEachFailure) {
  if (IsChildRun()) FailHook();
}

TEST(AfterEachFailure, Body) {}

TEST_AFTER(AfterFailure) {
  if (IsChildRun()) FailHook();
}

TEST(AfterFailure, Body) {}

TEST_AFTER_ALL(AfterAllFailure) {
  if (IsChildRun()) FailHook();
}

TEST(AfterAllFailure, Body) {}
