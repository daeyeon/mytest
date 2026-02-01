#include <mytest.h>
#include <mytest-report.h>

#include <string>
#include <vector>

int main(int argc, char* argv[]) {
  // Execute the tests with the -r option to generate a report.
  SET_REPORTER(mytest::GTestXmlReporter);
  return RUN_ALL_TESTS(argc, argv);
}

//-----------------------------------------------------------------------------
// 1: Basic Assertions
//-----------------------------------------------------------------------------

TEST(Assertions, Equality) {
  ASSERT_EQ(4, 2 + 2);
  EXPECT_NE(5, 2 + 2);
}

TEST(Assertions, Boolean) {
  ASSERT(true);
  EXPECT(true);
}

//-----------------------------------------------------------------------------
// 2: Test Hooks
//
// - TEST_BEFORE/AFTER(Group): Runs once before/after all tests in a group.
// - TEST_BEFORE/AFTER_EACH(Group): Runs before/after each test in a group.
//-----------------------------------------------------------------------------

static std::vector<std::string> hook_events;

TEST_BEFORE(Hooks) { hook_events.push_back("BEFORE"); }
TEST_BEFORE_EACH(Hooks) { hook_events.push_back("BEFORE_EACH"); }
TEST_AFTER_EACH(Hooks) { hook_events.push_back("AFTER_EACH"); }
TEST_AFTER(Hooks) { hook_events.push_back("AFTER"); }
TEST_AFTER_ALL(Hooks) {
  printf("AFTER_ALL\nHook Events:\n");
  for (const auto& event : hook_events) printf("  %s\n", event.c_str());
}

TEST(Hooks, Test1) {
  TEST_EXPECT_FAILURE();
  hook_events.push_back("TEST_1");
  EXPECT(false);
}

TEST(Hooks, Test2) { hook_events.push_back("TEST_2"); }

//-----------------------------------------------------------------------------
// 3: Advanced Features
//-----------------------------------------------------------------------------

TEST(Advanced, Skip) {
  TEST_SKIP("This test is intentionally skipped.");
  ASSERT(false);  // This line will not be executed.
}

TEST(Advanced, TimeoutExpected, 1000 /*ms*/) {
  // This test is expected to fail because it takes longer than 1000ms.
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
}

//-----------------------------------------------------------------------------
// 4: Isolated Tests
//
// TEST_ISOLATE runs a test in a separate process to prevent side effects.
//-----------------------------------------------------------------------------

static int global_counter = 0;

TEST_ISOLATE(Isolation, ChildProcess) {
  // This modification only affects the child process.
  global_counter = 100;
  ASSERT_EQ(global_counter, 100);
}

TEST(Isolation, MainProcess) {
  // This test runs in the main process and sees the original value.
  ASSERT_EQ(global_counter, 0);
}
