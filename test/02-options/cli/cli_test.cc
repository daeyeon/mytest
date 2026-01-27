#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>
#include "../../test_helpers.h"

#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static char test_app_exe[] = "out/1-basics.x";

// Execute self with given arguments and capture output
std::string ExecuteSelf2(const std::vector<std::string>& args) {
  std::string exe_path = GetExecutablePath();
  if (exe_path.empty()) return "";

  // Create pipe for stdout capture
  int pipe_fds[2];
  if (pipe(pipe_fds) == -1) return "";

  pid_t pid = fork();
  if (pid < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return "";
  }

  if (pid == 0) {
    // Child: redirect stdout/stderr to pipe
    close(pipe_fds[0]);
    dup2(pipe_fds[1], STDOUT_FILENO);
    dup2(pipe_fds[1], STDERR_FILENO);
    close(pipe_fds[1]);

    // Prepare arguments
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(exe_path.c_str()));
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    // Set environment variable to prevent infinite recursion
    setenv("MYTEST_CLI_TEST_MODE", "1", 1);

    execv(exe_path.c_str(), argv.data());
    _exit(1);  // execv failed
  }

  // Parent: read output
  close(pipe_fds[1]);

  std::string output;
  char buffer[4096];
  ssize_t bytes_read;
  while ((bytes_read = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
    output.append(buffer, bytes_read);
  }
  close(pipe_fds[0]);

  // Wait for child
  int status;
  waitpid(pid, &status, 0);

  return output;
}

// Check if we're in test mode (to prevent infinite recursion)
bool IsTestMode() { return getenv("MYTEST_CLI_TEST_MODE") != nullptr; }

// Only run CLI tests if not in test mode
TEST_ISOLATE(CliOptions, PatternInclude) {
  if (IsTestMode()) {
    TEST_SKIP("In test mode");
  }
  auto output = ExecuteSelf2({"-p", "DummyTest"});

  EXPECT_NE(output.find("1 test case(s) ran"), std::string::npos);
}

TEST_ISOLATE(CliOptions, PatternExclude) {
  if (IsTestMode()) {
    TEST_SKIP("In test mode");
  }

  auto output = ExecuteSelf2({"-p", "-CliOptions"});

  EXPECT_EQ(output.find("CliOptions"), std::string::npos);
}

TEST_ISOLATE(CliOptions, SilentMode) {
  if (IsTestMode()) {
    TEST_SKIP("In test mode");
  }

  std::string test_01_path = std::string(PROJECT_ROOT_DIR) + "/" + test_app_exe;

  auto output = ExecuteProc(test_01_path, {"-s", nullptr}, 10);

  EXPECT_NE(output.find("[==========]"), std::string::npos);
}

TEST_ISOLATE(CliOptions, ColorDisabled) {
  if (IsTestMode()) {
    TEST_SKIP("In test mode");
  }

  auto output = ExecuteSelf2({"-c"});

  EXPECT_EQ(output.find("\033["), std::string::npos);
}

TEST_ISOLATE(CliOptions, TimeoutOption) {
  if (IsTestMode()) {
    TEST_SKIP("In test mode");
  }

  auto output = ExecuteSelf2({"-t", "1000"});

  EXPECT_NE(output.find("test case(s) ran"), std::string::npos);
}

TEST_ISOLATE(CliOptions, HelpMessage) {
  if (IsTestMode()) {
    TEST_SKIP("In test mode");
  }
  auto output = ExecuteSelf2({"-h"});

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
  std::string exe_path = std::string(PROJECT_ROOT_DIR) + "/" + test_app_exe;
  bool result = VerifySnapshotOutput(
      exe_path, {"-p", "^Basic", "-c", "-s", nullptr}, "test/02-options/cli", "cli_test-basic.out");
  EXPECT_EQ(true, result);
}

TEST(SnapshotTests, IsolationTestsOutput) {
  std::string exe_path = std::string(PROJECT_ROOT_DIR) + "/" + test_app_exe;
  bool result = VerifySnapshotOutput(exe_path,
                                     {"-p", "^Isolation", "-c", "-s", nullptr},
                                     "test/02-options/cli",
                                     "cli_test-isolation.out");
  EXPECT_EQ(true, result);
}

TEST(SnapshotTests, MixTestsOutput) {
  std::string exe_path = std::string(PROJECT_ROOT_DIR) + "/" + test_app_exe;
  bool result = VerifySnapshotOutput(
      exe_path, {"-p", "^Mix", "-c", "-s", nullptr}, "test/02-options/cli", "cli_test-mix.out");
  EXPECT_EQ(true, result);
}
