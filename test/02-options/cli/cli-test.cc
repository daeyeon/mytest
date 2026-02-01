#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>
#include "../../test-helpers.h"

#include <filesystem>
#include <string>
#include <vector>

static char test_app_exe[] = BUILD_OUTPUT_DIR "/1-basics.x";

// Check if we're in test mode (to prevent infinite recursion)
bool IsTestMode() { return getenv("IS_SPAWNED_CHILD") != nullptr; }

// Only run CLI tests if not in test mode
TEST_ISOLATE(CliOptions, PatternInclude) {
  if (IsTestMode()) {
    TEST_SKIP("In test mode");
  }
  auto output = ExecuteSelf({"-p", "DummyTest"});

  EXPECT_NE(output.find("1 test case(s) ran"), std::string::npos);
}

TEST_ISOLATE(CliOptions, PatternExclude) {
  if (IsTestMode()) {
    TEST_SKIP("In test mode");
  }

  auto output = ExecuteSelf({"-p", "-CliOptions"});

  EXPECT_EQ(output.find("CliOptions"), std::string::npos);
}

TEST_ISOLATE(CliOptions, SilentMode) {
  if (IsTestMode()) {
    TEST_SKIP("In test mode");
  }

  std::string test_01_path = GetProjectRoot() + "/" + test_app_exe;

  auto output = ExecuteProc(test_01_path, {"-s", nullptr}, 10);

  EXPECT_NE(output.find("[==========]"), std::string::npos);
}

TEST_ISOLATE(CliOptions, ColorDisabled) {
  if (IsTestMode()) {
    TEST_SKIP("In test mode");
  }

  auto output = ExecuteSelf({"-c"});

  EXPECT_EQ(output.find("\033["), std::string::npos);
}

TEST_ISOLATE(CliOptions, TimeoutOption) {
  if (IsTestMode()) {
    TEST_SKIP("In test mode");
  }

  auto output = ExecuteSelf({"-t", "1000"});

  EXPECT_NE(output.find("test case(s) ran"), std::string::npos);
}

TEST_ISOLATE(CliOptions, HelpMessage) {
  if (IsTestMode()) {
    TEST_SKIP("In test mode");
  }
  auto output = ExecuteSelf({"-h"});

  EXPECT_NE(output.find("Options:"), std::string::npos);
  EXPECT_NE(output.find("-p"), std::string::npos);
  EXPECT_NE(output.find("-t"), std::string::npos);
  EXPECT_NE(output.find("-s"), std::string::npos);
}

TEST(DummyTest, ShouldAppearInPatternTest) { EXPECT_EQ(1, 1); }

TEST(OutputTest, PrintsToStdout) {
  std::cout << "THIS_IS_TEST_OUTPUT" << std::endl;
  EXPECT_EQ(1, 1);
}

TEST(SnapshotTests, BasicTestsOutput) {
  std::string exe_path = GetProjectRoot() + "/" + test_app_exe;
  bool result = VerifySnapshotOutput(
      exe_path, {"-p", "^Basic", "-c", "-s", nullptr}, "test/02-options/cli", "cli-test-basic.out");
  EXPECT_EQ(true, result);
}

TEST(SnapshotTests, IsolationTestsOutput) {
  std::string exe_path = GetProjectRoot() + "/" + test_app_exe;
  bool result = VerifySnapshotOutput(exe_path,
                                     {"-p", "^Isolation", "-c", "-s", nullptr},
                                     "test/02-options/cli",
                                     "cli-test-isolation.out");
  EXPECT_EQ(true, result);
}

TEST(SnapshotTests, MixTestsOutput) {
  std::string exe_path = GetProjectRoot() + "/" + test_app_exe;
  bool result = VerifySnapshotOutput(
      exe_path, {"-p", "^Mix", "-c", "-s", nullptr}, "test/02-options/cli", "cli-test-mix.out");
  EXPECT_EQ(true, result);
}
