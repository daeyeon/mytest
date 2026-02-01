/*
 * MyTest - Lean, hassle-free testing utility, my way.
 * Copyright 2024-present Daeyeon Jeong (daeyeon.dev@gmail.com)
 *
 * Licensed under the Apache License, Version 2.0.
 * See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#pragma once

#include <fcntl.h>
#include <limits.h>
#include <setjmp.h>  // sigsetjmp, siglongjmp
#include <spawn.h>
#include <sys/time.h>  // setitimer
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>  // dup, dup2, fileno, close
#ifdef __APPLE__
#include <mach-o/dyld.h>  // _NSGetExecutablePath
#endif
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

/*
  Usage:

  TEST(TestSuite, Equality) {
    ASSERT_EQ(4, 2 + 2);
    EXPECT_NE(5, 2 + 2);
  }

  TEST_BEFORE_EACH(TestSuite) { std::cout << "Before each test" << std::endl; }
  TEST_AFTER_EACH(TestSuite)  { std::cout << "After each test" << std::endl; }
  TEST_BEFORE(TestSuite)      { std::cout << "Before all tests" << std::endl; }
  TEST_AFTER(TestSuite)       { std::cout << "After all tests" << std::endl; }

  TEST(TestSuite, TestTimeout, 1000) {
    TEST_EXPECT_FAILURE();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ASSERT_EQ(1, 0);  // Unreachable due to timeout
  }

  TEST(TestSuite, TestSkip) {
    TEST_SKIP();
    ASSERT_EQ(1, 0);  // Unreachable: test skipped
  }

  TEST_ISOLATE(TestSuite, SegfaultIsolated) {
    int* p = nullptr;
    *p = 1;           // Segfaults here; main process safe via isolation
  }

  int main(int argc, char* argv[]) {
    return RUN_ALL_TESTS(argc, argv);
  }

  Command Line Usage Examples:
   ./your_test -s                // Silent mode, suppress stdout and stderr
   ./your_test -p "TestSuite"    // Run all tests in 'TestSuite' in the name
   ./your_test -p "-TestSuite"   // Exclude all tests in 'TestSuite' in the name

  Macros:
   TEST_SKIP              : Skip a test during execution.
   TEST_EXPECT_FAILURE    : Mark a test expected to fail (for negative tests).
   MYTEST_CONFIG_USE_MAIN : Define to use this utility's main function.
*/

namespace mytest {
struct TestResult {
  std::string suite;
  std::string name;
  bool failure = false;
  bool skipped = false;
  std::string message;
  std::vector<std::string> details;
};

struct Summary {
  int total = 0;
  int failures = 0;
  int skipped = 0;
};

struct Options {
  std::string output_path;
};

class Reporter {
 public:
  virtual ~Reporter() = default;
  virtual void OnComplete(const std::vector<TestResult>& results,
                          const Summary& summary,
                          const Options& options) = 0;
};
}  // namespace mytest

extern char** environ;

class MyTest {
 public:
  using TestResult = mytest::TestResult;
  using ReportSummary = mytest::Summary;
  using ReportOptions = mytest::Options;

  static MyTest& Instance() {
    static MyTest instance;
    return instance;
  }

  class TestSkipException : public std::runtime_error {
   public:
    explicit TestSkipException(const std::string& message = "Expected skipped.")
        : std::runtime_error("   Skipped : " + message) {}
  };

  class TestTimeoutException : public std::runtime_error {
   public:
    explicit TestTimeoutException(const std::string& message)
        : std::runtime_error(" Timed out : " + message) {}
  };

  class TestAssertException : public std::runtime_error {
   public:
    explicit TestAssertException(const std::string& message) : std::runtime_error(message) {}
  };

  void PrintCheckResult(const std::string& message) {
    if (silent_) SilenceOutput(false);
    std::cout << std::endl
              << colors(expect_failure_ ? RESET : RED) << message << colors(RESET) << std::endl;
    if (silent_) SilenceOutput(true);
    result_details_.push_back(message);
  }

#if !defined(UNUSED) && defined(__GNUC__)
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

  void SilenceOutput(bool silent) {
    static int stdout_backup = -1, stderr_backup = -1;
    if (silent) {
      if (stdout_backup != -1) return;  // Already silenced
      fflush(stdout);
      fflush(stderr);
      stdout_backup = dup(fileno(stdout));
      stderr_backup = dup(fileno(stderr));
      UNUSED FILE* fp1 = freopen("/dev/null", "w", stdout);
      UNUSED FILE* fp2 = freopen("/dev/null", "w", stderr);
    } else {
      if (stdout_backup == -1) return;  // Already not silenced
      if (stdout_backup != -1) {
        fflush(stdout);
        dup2(stdout_backup, fileno(stdout));
        close(stdout_backup);
        stdout_backup = -1;
      }
      if (stderr_backup != -1) {
        fflush(stderr);
        dup2(stderr_backup, fileno(stderr));
        close(stderr_backup);
        stderr_backup = -1;
      }
      std::cout.flush();
    }
  }

  // clang-format off
  void RegisterTest(const std::string& group_name, std::function<void()> test, std::optional<int> timeout = std::nullopt, const std::string& loc = "") { tests_.emplace_back(group_name, std::move(test)); if (timeout) test_timeouts_[group_name] = *timeout; if (!loc.empty()) locations_[group_name] = loc;}
  void RegisterTestBeforeEach(const std::string& group_name, std::function<void()> test) { test_before_each_[group_name] = test; }
  void RegisterTestAfterEach(const std::string& group_name, std::function<void()> test) { test_after_each_[group_name] = test; }
  void RegisterTestBefore(const std::string& group_name, std::function<void()> test) { test_before_[group_name] = test; }
  void RegisterTestAfter(const std::string& group_name, std::function<void()> test) { test_after_[group_name] = test; }
  void RegisterTestAfterAll(const std::string& group_name, std::function<void()> test) { test_after_all_[group_name] = test; }
  void MarkConditionPassed(bool value) { condition_passed_ = value; }
  void MarkExpectFailure(bool value) { expect_failure_ = value; }
  void AddExcludePattern(const std::string& pattern) { exclude_patterns_.emplace_back(pattern); }
  void RegisterProcessTest(const std::string& name) { process_tests_.insert(name); }
  void RegisterPostTestTask(std::function<void()> task) { post_test_tasks_.push_back(std::move(task)); }
  void SetReporter(std::shared_ptr<mytest::Reporter> reporter) { reporter_ = std::move(reporter); }
  static std::optional<int> MakeTimeout() { return std::nullopt; }
  template <typename T>
  static std::optional<int> MakeTimeout(T value) { return std::optional<int>{static_cast<int>(value)}; }
  static bool IsMainProcess() {
    static const pid_t cached_main_pid = []() { if (const char* env_pid = getenv(MyTest::kMainPidEnv)) { return static_cast<pid_t>(std::strtol(env_pid, nullptr, 10)); } return getpid(); }();
    return getpid() == cached_main_pid;
  }
  static const char* GetCurrentTestName() { return current_test_name_.c_str(); }
  bool ShouldRunInProcess(const std::string& name) const { return process_tests_.count(name) > 0; }
  bool IsIsolated(std::optional<std::string> maybe_name = std::nullopt) const { return job_isolation_ || (maybe_name ? ShouldRunInProcess(*maybe_name) : false); }
  int GetTestTimeout(const std::string& name) const { auto it = test_timeouts_.find(name); return it != test_timeouts_.end() ? it->second : timeout_; }
  static bool GetExecutablePath(char* path, size_t size) {
#ifdef __APPLE__
    char resolved[PATH_MAX];
    uint32_t bufsize = static_cast<uint32_t>(size);
    return _NSGetExecutablePath(path, &bufsize) == 0 && realpath(path, resolved) && (strncpy(path, resolved, size - 1), path[size - 1] = '\0', true);
#else
    ssize_t len = readlink("/proc/self/exe", path, size - 1);
    return len > 0 && (path[len] = '\0', true);
#endif
  }
  // clang-format on

  bool force() { return force_; }
  bool silent() { return silent_; }
  int timeout() { return timeout_; }
  enum colors { RESET, GREEN, RED, YELLOW, NUM_COLORS };
  const char* colors(size_t idx) { return colors_[idx]; }

  int RunAllTests(int argc, char* argv[]) {
    setbuf(stdout, nullptr);
    setbuf(stderr, nullptr);

    if (getenv(kMainPidEnv) == nullptr) {
      const std::string main_pid = std::to_string(getpid());
      setenv(kMainPidEnv, main_pid.c_str(), 1);
      char cwd_buf[PATH_MAX];
      if (getcwd(cwd_buf, sizeof(cwd_buf)) != nullptr) {
        setenv(kInitialCwdEnv, cwd_buf, 1);
      }
    }

    bool is_spawned = (getenv(kSpawnedEnv) != nullptr);
    if (is_spawned) {  // Restore the test environment if spawned.
      unsetenv(kSpawnedEnv);
      const char* initial_cwd = getenv(kInitialCwdEnv);
      if (initial_cwd != nullptr) {
        if (chdir(initial_cwd) != 0) {
          // This will likely cause the test to fail, which is intended.
        }
      }
    }
    std::vector<std::regex> include_patterns;
    bool use_report = false;
    std::string report_output_path;
    test_results_.clear();

    // Parse CLI arguments
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      // clang-format off
      if (arg == "-p" && i + 1 < argc) {
        std::string pattern = argv[++i];
        try {
          (pattern[0] == '-') ? exclude_patterns_.emplace_back(pattern.substr(1)) : include_patterns.emplace_back(pattern);
        } catch (const std::exception& e) {
          std::cerr << e.what() << std::endl;
          return 1;
        }
      } else if (arg == "-t" && i + 1 < argc) { timeout_ = std::stoi(argv[++i]);
      } else if (arg == "-c") { use_color_ = false;
      } else if (arg == "-s") { silent_ = true;
      } else if (arg == "-f") { force_ = true;
      } else if (arg == "-j") { job_isolation_ = true;
      } else if (arg == "-r") {
        if (reporter_ == nullptr) {
          std::cerr << "Report requested but no report writer registered." << std::endl;
          return 1;
        }
        use_report = true;
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          report_output_path = argv[++i];
        } else {
          report_output_path.clear();
        }
      } else if (arg == "-h" || arg == "--help") { return PrintUsage(argv[0]); }
      // clang-format on
    }

    auto should_run = [this, &include_patterns](const std::string& name) {
      for (const auto& pattern : exclude_patterns_) {
        if (std::regex_search(name, pattern)) return false;
      }
      if (include_patterns.empty()) return true;
      for (const auto& pattern : include_patterns) {
        if (std::regex_search(name, pattern)) return true;
      }
      return false;
    };

    // Run tests
    int num_success = 0, num_failure = 0, num_skipped = 0;
    int num_ran_tests = 0, num_filtered_tests = 0;

    colors_ = {use_color_ ? "\033[0m" : "",
               use_color_ ? "\033[32m" : "",
               use_color_ ? "\033[31m" : "",
               use_color_ ? "\033[33m" : ""};
    const char** colors = colors_.data();

    // Categorize tests while preserving registration order of groups.
    std::unordered_map<std::string, std::vector<TestPair>> categorized_tests;
    std::unordered_map<std::string, bool> group_has_normal;
    std::vector<std::string> group_order;
    for (const auto& test_pair : tests_) {
      const std::string& name = test_pair.first;
      if (!should_run(name)) continue;

      num_filtered_tests++;
      auto group_name = name.substr(0, name.find(':'));
      if (!categorized_tests.count(group_name)) group_order.push_back(group_name);
      categorized_tests[group_name].push_back(test_pair);
      if (process_tests_.count(name) == 0) group_has_normal[group_name] = true;
    }

    // clang-format off
    if (!is_spawned) {
      printf("%s[==========]%s Running %d test case(s).\n", colors[GREEN], colors[RESET], num_filtered_tests);
    }
    auto PrintStart = [&colors, this](const std::string& name) {
                         printf("%s[ RUN      ]%s %s", colors[GREEN], colors[RESET], name.c_str());
                         if (IsIsolated(name)) printf(" (PID: %d)", getpid());
                         printf("\n");
    };
    auto PrintEnd = [&colors](const bool failure, const bool skipped, const std::string& name) {
      if (failure)       printf("%s[  FAILED  ]%s %s\n", colors[RED], colors[RESET], name.c_str());
      else if (skipped)  printf("%s[  SKIPPED ]%s %s\n", colors[YELLOW], colors[RESET], name.c_str());
      else               printf("%s[       OK ]%s %s\n", colors[GREEN], colors[RESET], name.c_str());
    };
    auto PrintResult = [&]() {
                         printf("%s[==========]%s %d test case(s) ran.\n", colors[GREEN], colors[RESET], num_ran_tests);
                         printf("%s[  PASSED  ]%s %d test(s)\n", colors[GREEN], colors[RESET], num_ran_tests - num_failure - num_skipped);
    (num_skipped > 0) && printf("%s[  SKIPPED ]%s %d test(s)\n", colors[YELLOW], colors[RESET], num_skipped);
    (num_failure > 0) && printf("%s[  FAILED  ]%s %d test(s)\n", colors[RED], colors[RESET], num_failure);
    };
    // clang-format on

    using ExecResult = std::tuple<bool, bool, std::string, std::vector<std::string>>;
    auto Clean = [](std::string s) {
      return std::regex_replace(s, std::regex("\x1B\\[[0-9;]*[mK]"), "")  // Strips ANSI codes
          .erase(0, s.find_first_not_of(" \t\n\r"));
    };

    auto HookCaller = [this,
                       &colors](std::function<void()> func) -> std::tuple<bool, bool, std::string> {
      std::string message;
      bool failure = false, skipped = false;
      condition_passed_ = true, expect_failure_ = false;  // reset

      try {
        auto _ = OnScopeLeave::create([this, &failure]() {
          if (!condition_passed_) failure = true;
          SilenceOutput(false);
        });
        SilenceOutput(silent_);
        func();
      } catch (const TestSkipException& e) {
        skipped = true, message = e.what();
        printf("\n%s\n", e.what());
      } catch (const TestAssertException& e) {
        failure = true, message = e.what();
        // the failure message is already printed in PrintTestResult.
      } catch (const TestTimeoutException& e) {
        failure = true, message = e.what();
        if (locations_.count(current_test_name_)) message += " " + locations_[current_test_name_];
        printf("\n%s\n", message.c_str());
      } catch (const std::exception& e) {
        failure = true, message = e.what();
        if (locations_.count(current_test_name_)) message += " " + locations_[current_test_name_];
        printf("\nException : %s\n", message.c_str());
      } catch (...) {
        failure = true, message = "Unknown exception";
        if (locations_.count(current_test_name_)) message += " " + locations_[current_test_name_];
        printf("\nException : %s\n", message.c_str());
      }
      if (expect_failure_) {
        failure = !failure;
        if (failure) {
          printf("    Failed : Expected fail but passed.\n");
        } else {
          printf("    Passed : Expected fail and failed.\n");
        }
      }
      return {failure, skipped, message};
    };

    auto NormalTestExecutor = [this, &colors, &HookCaller](
                                  const std::string& name,
                                  const TestFunction& test,
                                  const std::string& group_name) -> ExecResult {
      result_details_.clear();
      auto CallHook = [this, &group_name](const auto& hooks) {
        if (!group_name.empty() && hooks.count(group_name)) hooks.at(group_name)();
      };

      bool should_call_after_hook = false;
      auto [failure, skipped, message] = HookCaller([&]() {
        auto post_task_runner = OnScopeLeave::create([this]() { RunPostTestTask(); });
        CallHook(test_before_each_);
        should_call_after_hook = true;
        test();
      });

      if (should_call_after_hook) {
        auto [after_failure, _skipped, _] = HookCaller([&]() { CallHook(test_after_each_); });
        failure = failure || after_failure;
      }

      if (failure && message.empty()) message = "See console output.";
      if (skipped && message.empty()) message = "Skipped.";
      return {failure, skipped, message, result_details_};
    };

    auto IsolatedTestExecutor = [this, &colors, &Clean](
                                    const std::string& name,
                                    const TestFunction& test,
                                    const std::string group_name = "") -> ExecResult {
      char exe_path[PATH_MAX];
      if (!GetExecutablePath(exe_path, sizeof(exe_path))) {
        return {true, false, "Failed to resolve executable path", std::vector<std::string>{}};
      }

      const int process_timeout = GetTestTimeout(name);
      int parent_timeout = process_timeout;
      if (test_timeouts_.find(name) != test_timeouts_.end()) {
        parent_timeout = process_timeout + 200;  // Wait 0.2s more as grace period
      }

      std::string timeout = std::to_string(process_timeout);
      std::string pattern = name + "$";
      std::vector<const char*> argv_vec = {exe_path, "-p", pattern.c_str(), "-t", timeout.c_str()};
      if (!use_color_) argv_vec.push_back("-c");
      if (silent_) argv_vec.push_back("-s");
      if (job_isolation_) argv_vec.push_back("-j");
      argv_vec.push_back(nullptr);

      // Create pipe to capture child's stdout
      int pipe_fd[2];
      if (pipe(pipe_fd) == -1) {
        return {true, false, "Failed to create pipe", std::vector<std::string>{}};
      }

      posix_spawn_file_actions_t actions;
      posix_spawn_file_actions_init(&actions);
      posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDOUT_FILENO);
      posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDERR_FILENO);
      posix_spawn_file_actions_addclose(&actions, pipe_fd[0]);
      posix_spawn_file_actions_addclose(&actions, pipe_fd[1]);

      pid_t worker_pid = -1;
      setenv(kSpawnedEnv, "1", 1);
      int spawn_ret = posix_spawn(&worker_pid,
                                  exe_path,
                                  &actions,
                                  nullptr,
                                  const_cast<char* const*>(argv_vec.data()),
                                  environ);
      unsetenv(kSpawnedEnv);
      posix_spawn_file_actions_destroy(&actions);
      close(pipe_fd[1]);  // Close write end in parent

      if (spawn_ret != 0) {
        close(pipe_fd[0]);
        return {true, false, "Failed to spawn worker process", std::vector<std::string>{}};
      }

      // Read child's output while waiting
      std::string child_output;
      char buf[4096];
      int flags = fcntl(pipe_fd[0], F_GETFL, 0);
      fcntl(pipe_fd[0], F_SETFL, flags | O_NONBLOCK);

      int status = 0, exit_code = -1;
      bool sig_timed_out = false;
      auto start_time = std::chrono::steady_clock::now();
      auto timeout_ms = std::chrono::milliseconds(parent_timeout);
      while (true) {
        // Read available output from pipe
        ssize_t n;
        while ((n = read(pipe_fd[0], buf, sizeof(buf) - 1)) > 0) {
          buf[n] = '\0';
          child_output += buf;
        }

        int wait_ret = waitpid(worker_pid, &status, WNOHANG);
        if (wait_ret > 0) {
          // Read remaining output
          while ((n = read(pipe_fd[0], buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            child_output += buf;
          }
          if (WIFEXITED(status)) {
            exit_code = WEXITSTATUS(status);
          } else if (WIFSIGNALED(status)) {
            exit_code = 128 + WTERMSIG(status);
          }
          break;
        } else if (wait_ret < 0) {
          exit_code = -1;
          break;
        }
        // Check timeout
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
        if (elapsed >= timeout_ms) {
          kill(worker_pid, SIGKILL);
          waitpid(worker_pid, &status, 0);
          sig_timed_out = true;
          exit_code = 1;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      close(pipe_fd[0]);

      // Print captured child output
      if (!child_output.empty()) {
        printf("%s", child_output.c_str());
        fflush(stdout);
      }

      bool failure = false, skipped = false;
      std::string message;
      if (sig_timed_out) {
        failure = true;  // Signal timeout is always a failure
        message = std::string(" Timed out : ") + name;
      } else if (exit_code > 128 && exit_code < 255) {
        failure = true;  // Signal exits: 128 + signal_number (e.g., 137 = SIGKILL)
        int sig = exit_code - 128;
        const char* sig_name = strsignal(sig);
        message = std::string("Terminated by signal ") + std::to_string(sig) +
                  (sig_name ? (std::string(" (") + sig_name + ")") : "");
        if (locations_.count(name)) message += " " + locations_[name];
        printf("\n%s%s%s\n", colors[RED], message.c_str(), colors[RESET]);
      } else if (exit_code >= 0 && exit_code < 16) {
        // bit 0 (1): FAILURE, bit 1 (2): SKIPPED, bit 2 (4): TIMEOUT, bit 3 (8): EXPECT_FAILURE
        failure = exit_code & 1;
        bool is_skipped = exit_code & 2;
        bool is_timeout = exit_code & 4;
        bool is_expect_failure = exit_code & 8;
        if (is_skipped) {
          skipped = true;
          printf("\n   Skipped : Expected skipped.\n");
        }
        if (is_timeout) {
          message = std::string(" Timed out : ") + name;
          if (locations_.count(name)) message += " " + locations_[name];
          printf("\n%s\n", message.c_str());
        }
        if (is_expect_failure) {
          if (failure) {
            printf("    Failed : Expected fail but passed.\n");
          } else {
            printf("    Passed : Expected fail and failed.\n");
          }
        }
        if (failure && message.empty()) message = "See console output.";
        if (skipped && message.empty()) message = "Skipped.";
      } else {
        failure = true, message = "Unknown exit code or crash: " + std::to_string(exit_code);
        if (locations_.count(name)) message += " " + locations_[name];
        printf("\n%s%s%s\n", colors[RED], message.c_str(), colors[RESET]);
      }
      std::vector<std::string> details;
      std::stringstream ss(child_output);
      for (std::string line; std::getline(ss, line);) {
        if (std::string clean = Clean(line);
            clean.find(" failed (") != std::string::npos && clean.find(')') != std::string::npos) {
          details.push_back(std::move(clean));
        }
      }
      return std::make_tuple(failure, skipped, message, details);
    };

    enum ExitFlag { SUCCESS = 0, FAILURE = 1, SKIPPED = 2, TIMEOUT = 4, EXPECT_FAILURE = 8 };

    auto SpawnedTestExecutor =
        [this](const std::string& name,
               const TestFunction& test,
               const std::string& group_name) -> std::tuple<bool, bool, bool, bool> {
      condition_passed_ = true, expect_failure_ = false, current_test_name_ = name;
      bool failure = false, skipped = false, timeout = false;
      auto CallHook = [this, &group_name](const auto& hooks) {
        if (!group_name.empty() && hooks.count(group_name)) hooks.at(group_name)();
      };
      try {
        auto _ = OnScopeLeave::create([&, this]() { SilenceOutput(false); });
        SilenceOutput(silent_);
        CallHook(test_before_);
        CallHook(test_before_each_);
        {
          auto post_runner = OnScopeLeave::create([this]() { RunPostTestTask(); });
          test();
        }
        failure = !condition_passed_;
        CallHook(test_after_each_);
        CallHook(test_after_);
      } catch (const TestSkipException& e) {
        skipped = true;
      } catch (const TestTimeoutException& e) {
        failure = true, timeout = true;
      } catch (...) {
        failure = true;
      }
      // Invert failure if expect_failure_ is set
      if (expect_failure_) failure = !failure;
      return {failure, skipped, timeout, expect_failure_};
    };

    if (is_spawned) {
      for (const auto& test_pair : tests_) {
        const std::string& name = test_pair.first;
        if (!should_run(name)) continue;
        const auto group_name = name.substr(0, name.find(':'));
        auto [failure, skipped, timeout, expect_fail] =
            SpawnedTestExecutor(name, test_pair.second, group_name);
        int code = SUCCESS;
        if (failure) code |= FAILURE;
        if (skipped) code |= SKIPPED;
        if (timeout) code |= TIMEOUT;
        if (expect_fail) code |= EXPECT_FAILURE;
        return code;
      }
      return FAILURE;
    }

    auto TestDispatcher = [this, &NormalTestExecutor, &IsolatedTestExecutor](
                              const std::string& name,
                              const TestFunction& test,
                              const std::string group_name = "",
                              bool run_in_process = false) -> ExecResult {
      condition_passed_ = true, expect_failure_ = false, current_test_name_ = name;
      auto [failure, skipped, message, details] = run_in_process
                                                      ? IsolatedTestExecutor(name, test, group_name)
                                                      : NormalTestExecutor(name, test, group_name);
      while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
        message.pop_back();
      }
      return std::make_tuple(failure, skipped, message, details);
    };

    for (const auto& group_name : group_order) {
      const auto& group_tests = categorized_tests[group_name];
      bool group_failure = false;
      bool group_skipped = false;
      const bool has_normal_tests = group_has_normal[group_name];

      PrintStart(group_name);

      if (!job_isolation_ && has_normal_tests && test_before_.count(group_name)) {
        auto [failure, skipped, message, details] =
            TestDispatcher(group_name, test_before_[group_name]);
        if (skipped) {
          group_skipped = true;
          continue;
        }
        group_failure = group_failure || failure;
      }

      for (const auto& group_test : group_tests) {
        const std::string& name = group_test.first;
        const TestFunction& test = group_test.second;

        PrintStart(name);

        auto [failure, skipped, message, details] =
            TestDispatcher(name, test, group_name, IsIsolated(name));
        (failure ? ++num_failure : (skipped ? ++num_skipped : ++num_success));
        ++num_ran_tests;

        if (failure && !expect_failure_) printf("\n");
        PrintEnd(failure, skipped, name);

        auto colon = name.find(':');
        if (colon != std::string::npos) {
          TestResult result{name.substr(0, colon),   // suite
                            name.substr(colon + 1),  // name
                            failure,
                            skipped,
                            std::move(message),
                            std::move(details)};
          while (!result.message.empty() &&
                 (result.message.back() == '\n' || result.message.back() == '\r')) {
            result.message.pop_back();
          }
          test_results_.push_back(std::move(result));
        }
        group_failure = group_failure || failure;
      }

      if (!job_isolation_ && has_normal_tests && test_after_.count(group_name)) {
        auto [failure, _1, _2, _3] = TestDispatcher(group_name, test_after_[group_name]);
        group_failure = group_failure || failure;
        if (failure) num_failure++;
      }

      if (test_after_all_.count(group_name)) {
        auto [failure, _1, _2, _3] = TestDispatcher(group_name, test_after_all_[group_name]);
        group_failure = group_failure || failure;
        if (failure) num_failure++;
      }

      PrintEnd(group_failure, group_skipped, group_name);
    }

    PrintResult();
    if (num_failure > 0) {
      printf("\n %d FAILED TEST%s\n", num_failure, num_failure > 1 ? "S" : "");
      for (const auto& r : test_results_) {
        if (!r.failure) continue;
        printf(" - %s:%s\n", r.suite.c_str(), r.name.c_str());
        for (const auto& f : r.details) printf("  - %.*s\n", (int)f.find('\n'), f.c_str());
        if (r.details.empty() && !r.message.empty()) printf("   - %s\n", Clean(r.message).c_str());
      }
    }
    if (reporter_ && use_report) {
      ReportSummary summary{num_ran_tests, num_failure, num_skipped};
      ReportOptions options;
      options.output_path = report_output_path;
      reporter_->OnComplete(test_results_, summary, options);
    }
    return num_failure > 0 ? 1 : 0;
  }

#if !defined(WARN_UNUSED_RESULT) && defined(__GNUC__)
#define WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))
#else
#define WARN_UNUSED_RESULT
#endif

  class OnScopeLeave {
   public:
    // clang-format off
    using Function = std::function<void()>;
    OnScopeLeave(const OnScopeLeave& other) = delete;
    OnScopeLeave& operator=(const OnScopeLeave& other) = delete;
    explicit OnScopeLeave(Function&& function) : function_(std::move(function)) {}
    OnScopeLeave(OnScopeLeave&& other) : function_(std::move(other.function_)) { other.function_ = nullptr; }
    ~OnScopeLeave() { if (function_) function_(); }
    static WARN_UNUSED_RESULT OnScopeLeave create(Function&& function) { return OnScopeLeave(std::move(function)); }
    // clang-format on
   private:
    Function function_;
  };

  static sigjmp_buf& timeout_jmp_buf() { return timeout_jmp_buf_; }
  static void TimeoutHandler(int signum) {
    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, nullptr);
    siglongjmp(MyTest::timeout_jmp_buf(), 1);
  }

  class TimeoutScope {
   public:
    TimeoutScope(int timeout_ms, void (*signal_handler)(int)) {
      sigaction(SIGALRM, nullptr, &old_sa_);
      getitimer(ITIMER_REAL, &old_timer_);
      struct sigaction sa;
      sa.sa_handler = signal_handler;
      sigemptyset(&sa.sa_mask);
      sa.sa_flags = SA_RESTART;
      sigaction(SIGALRM, &sa, nullptr);
      struct itimerval timer;
      timer.it_value.tv_sec = timeout_ms / 1000;
      timer.it_value.tv_usec = (timeout_ms % 1000) * 1000;
      timer.it_interval.tv_sec = 0;
      timer.it_interval.tv_usec = 0;
      setitimer(ITIMER_REAL, &timer, nullptr);
    }
    ~TimeoutScope() {
      setitimer(ITIMER_REAL, &old_timer_, nullptr);
      sigaction(SIGALRM, &old_sa_, nullptr);
    }

   private:
    struct sigaction old_sa_;
    struct itimerval old_timer_;
  };

 private:
  static constexpr int kDefaultTimeoutMS = 60000;
  static constexpr const char* kMainPidEnv = "MYTEST_MAIN_PID";
  static constexpr const char* kInitialCwdEnv = "MYTEST_INITIAL_CWD";
  static constexpr const char* kSpawnedEnv = "MYTEST_SPAWNED_CHILD";
  static constexpr const char* kCalVersion = "26.01.31";
  inline static std::string current_test_name_;

  // clang-format off
  int PrintUsage(const char* name) {
    std::cout
      << "Usage: " << name << " [options]\n"
      << "Options:\n"
      << "  -p \"PATTERN\"  : Include tests matching PATTERN\n"
      << "  -p \"-PATTERN\" : Exclude tests matching PATTERN\n"
      << "  -t TIMEOUT    : Set the timeout value in milliseconds (default: " << timeout_ << ")\n"
      << "  -c            : Disable color output\n"
      << "  -f            : Force mode, run all tests, including skipped ones\n"
      << "  -j            : Job mode, run all tests in separate processes\n"
      << "  -s            : Silent mode (suppress stdout and stderr output)\n"
      << "  -r [FILE]     : Write report via registered reporter (optional FILE)\n"
      << "  -h, --help    : Show this help message\n\n"
      << "Tests executed by the integrated testing utility, MyTest (v" << kCalVersion << ")\n";
    // clang-format on
    return 0;
  }

  void RunPostTestTask() { for (const auto& task : post_test_tasks_) task(); post_test_tasks_.clear(); }
  // clang-format on

  int timeout_{kDefaultTimeoutMS};
  bool force_{false};
  bool job_isolation_{false};
  bool use_color_{true};
  bool silent_{false};
  bool condition_passed_{true};
  bool expect_failure_{false};
  inline static sigjmp_buf timeout_jmp_buf_{};
  std::vector<const char*> colors_;
  std::vector<std::regex> exclude_patterns_;
  std::unordered_set<std::string> process_tests_;
  std::unordered_map<std::string, int> test_timeouts_;
  std::unordered_map<std::string, std::string> locations_;
  std::vector<std::function<void()>> post_test_tasks_;
  std::vector<TestResult> test_results_;
  std::vector<std::string> result_details_;
  std::shared_ptr<mytest::Reporter> reporter_;

  using TestFunction = std::function<void()>;
  using TestPair = std::pair<std::string, TestFunction>;
  std::vector<TestPair> tests_;
  std::unordered_map<std::string, std::function<void()>> test_before_each_;
  std::unordered_map<std::string, std::function<void()>> test_after_each_;
  std::unordered_map<std::string, std::function<void()>> test_before_;
  std::unordered_map<std::string, std::function<void()>> test_after_;
  std::unordered_map<std::string, std::function<void()>> test_after_all_;
  MyTest() {}
  MyTest(const MyTest&) = delete;
  MyTest& operator=(const MyTest&) = delete;
};

#define TEST_INTERNAL(force_process, group, name, ...)                                             \
  void group##name##_impl();                                                                       \
  void group##name(int timeout_ms = MyTest::Instance().timeout()) {                                \
    if (sigsetjmp(MyTest::timeout_jmp_buf(), 1) == 0) {                                            \
      MyTest::TimeoutScope timeout(timeout_ms, MyTest::TimeoutHandler);                            \
      group##name##_impl();                                                                        \
    } else {                                                                                       \
      throw MyTest::TestTimeoutException(#group ":" #name);                                        \
    }                                                                                              \
  }                                                                                                \
  struct group##name##_Register {                                                                  \
    group##name##_Register() {                                                                     \
      MyTest::Instance().RegisterTest(                                                             \
          #group ":" #name,                                                                        \
          []() { return group##name(__VA_ARGS__); },                                               \
          MyTest::Instance().MakeTimeout(__VA_ARGS__),                                             \
          _LOC);                                                                                   \
      if (force_process) {                                                                         \
        MyTest::Instance().RegisterProcessTest(#group ":" #name);                                  \
      }                                                                                            \
    }                                                                                              \
  } group##name##_register;

#define TEST(group, name, ...)                                                                     \
  TEST_INTERNAL(false, group, name, __VA_ARGS__)                                                   \
  void group##name##_impl()

#define TEST_ISOLATE(group, name, ...)                                                             \
  TEST_INTERNAL(true, group, name, __VA_ARGS__)                                                    \
  void group##name##_impl()

#define TEST_BEFORE_EACH(group)                                                                    \
  void group##_BeforeEach();                                                                       \
  struct group##_BeforeEach_Register {                                                             \
    group##_BeforeEach_Register() {                                                                \
      MyTest::Instance().RegisterTestBeforeEach(#group, group##_BeforeEach);                       \
    }                                                                                              \
  } group##_BeforeEach_register;                                                                   \
  void group##_BeforeEach()

#define TEST_AFTER_EACH(group)                                                                     \
  void group##_AfterEach();                                                                        \
  struct group##_AfterEach_Register {                                                              \
    group##_AfterEach_Register() {                                                                 \
      MyTest::Instance().RegisterTestAfterEach(#group, group##_AfterEach);                         \
    }                                                                                              \
  } group##_AfterEach_register;                                                                    \
  void group##_AfterEach()

#define TEST_BEFORE(group)                                                                         \
  void group##_Before();                                                                           \
  struct group##_Before_Register {                                                                 \
    group##_Before_Register() { MyTest::Instance().RegisterTestBefore(#group, group##_Before); }   \
  } group##_Before_register;                                                                       \
  void group##_Before()

#define TEST_AFTER(group)                                                                          \
  void group##_After();                                                                            \
  struct group##_After_Register {                                                                  \
    group##_After_Register() { MyTest::Instance().RegisterTestAfter(#group, group##_After); }      \
  } group##_After_register;                                                                        \
  void group##_After()

#define TEST_AFTER_ALL(group)                                                                      \
  void group##_AfterAll();                                                                         \
  struct group##_AfterAll_Register {                                                               \
    group##_AfterAll_Register() {                                                                  \
      MyTest::Instance().RegisterTestAfterAll(#group, group##_AfterAll);                           \
    }                                                                                              \
  } group##_AfterAll_register;                                                                     \
  void group##_AfterAll()

#define TEST_SKIP(message)                                                                         \
  do {                                                                                             \
    if (!MyTest::Instance().force()) {                                                             \
      throw MyTest::TestSkipException(message);                                                    \
    }                                                                                              \
  } while (0)

#define TEST_EXPECT_FAILURE(message)                                                               \
  do {                                                                                             \
    MyTest::Instance().MarkExpectFailure(true);                                                    \
  } while (0)

#define SET_REPORTER(Reporter) MyTest::Instance().SetReporter(std::make_shared<Reporter>());

#define RUN_ALL_TESTS(argc, argv) MyTest::Instance().RunAllTests(argc, argv)

#define IS_MAIN_PROCESS() MyTest::IsMainProcess()

#define TEST_NAME() MyTest::Instance().GetCurrentTestName()

#ifdef MYTEST_CONFIG_USE_MAIN
int main(int argc, char* argv[]) { return RUN_ALL_TESTS(argc, argv); }
#endif

#define _LOC                                                                                       \
  "(" + (std::string((strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__))) + ":" +    \
      std::to_string(__LINE__) + ")"

#define _FORMAT_ERROR_MESSAGE(x, cond_str, y, message)                                             \
  std::stringstream __ss;                                                                          \
  __ss << message << std::string(" ") + _LOC << "\n";                                              \
  __ss << "  Expected : (" << #x << " " cond_str " " << #y << ")\n";                               \
  __ss << "    Actual : (" << x << " " cond_str " " << y << ")";

#define _CHECK_CONDITION(cond, x, y, cond_str, message, should_throw)                              \
  do {                                                                                             \
    if (cond) {                                                                                    \
      _FORMAT_ERROR_MESSAGE(x, y, cond_str, message);                                              \
      MyTest::Instance().PrintCheckResult(__ss.str());                                             \
      if (should_throw) {                                                                          \
        throw MyTest::TestAssertException(__ss.str());                                             \
      } else {                                                                                     \
        MyTest::Instance().MarkConditionPassed(false);                                             \
      }                                                                                            \
    }                                                                                              \
  } while (0)

#define _EXPECT(cond, x, c, y, m) _CHECK_CONDITION(!(cond), x, #c, y, m, false)
#define _ASSERT(cond, x, c, y, m) _CHECK_CONDITION(!(cond), x, #c, y, m, true)
#define EXPECT_EQ(x, y) _EXPECT((x) == (y), x, ==, y, "EXPECT_EQ failed")
#define EXPECT_NE(x, y) _EXPECT((x) != (y), x, !=, y, "EXPECT_NE failed")
#define ASSERT_EQ(x, y) _ASSERT((x) == (y), x, ==, y, "ASSERT_EQ failed")
#define ASSERT_NE(x, y) _ASSERT((x) != (y), x, !=, y, "ASSERT_NE failed")

#define EXPECT(cond) EXPECT_EQ(static_cast<bool>(cond), true)
#define ASSERT(cond) ASSERT_EQ(static_cast<bool>(cond), true)
