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
#include <setjmp.h>
#include <spawn.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

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
  virtual void OnComplete(const std::vector<TestResult>& results, const Summary& summary,
                          const Options& options) = 0;
};
// clang-format off
template <typename T, typename = void> struct IsStreamable : std::false_type {};
template <typename T> struct IsStreamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>> : std::true_type {};
template <typename T> std::string FormatValue(const T& value) { std::stringstream ss; if constexpr (IsStreamable<T>::value) ss << value; else ss << "<unprintable>"; return ss.str(); }
// clang-format on
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
        : std::runtime_error("Skipped : " + message) {}
  };

  class TestTimeoutException : public std::runtime_error {
   public:
    explicit TestTimeoutException(const std::string& message)
        : std::runtime_error("Timed out : " + message) {}
  };

  class TestAssertException : public std::runtime_error {
   public:
    explicit TestAssertException(const std::string& message) : std::runtime_error(message) {}
  };

  void PrintCheckResult(const std::string& message) {
    if (silent_) SilenceOutput(false);
    std::cout << "\n" << colors(expect_failure_ ? RESET : RED) << message << colors(RESET) << "\n";
    if (silent_) SilenceOutput(true);
    result_details_.push_back(message);
  }

  void SilenceOutput(bool silent) {
    // clang-format off
    static int backups[2] = {-1, -1}; FILE* streams[2] = {stdout, stderr};
    if (silent) { if (backups[0] != -1) return; std::cout.flush(); for (int i : {0, 1}) { fflush(streams[i]); backups[i] = dup(fileno(streams[i])); std::ignore = freopen("/dev/null", "w", streams[i]); } }
    else { if (backups[0] == -1) return; std::cout.flush(); for (int i : {0, 1}) { fflush(streams[i]); dup2(backups[i], fileno(streams[i])); close(backups[i]); backups[i] = -1; } std::cout.clear(); }
    // clang-format on
  }

  class SilenceScope {
   public:
    SilenceScope() : active_(!MyTest::Instance().silent()) {
      if (active_ && depth_++ == 0) MyTest::Instance().SilenceOutput(true);
    }
    ~SilenceScope() {
      if (active_ && --depth_ == 0) MyTest::Instance().SilenceOutput(false);
    }
    SilenceScope(const SilenceScope&) = delete;
    SilenceScope& operator=(const SilenceScope&) = delete;

   private:
    bool active_;
    inline static int depth_{0};
  };

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
  void SetTempRoot(std::filesystem::path path) { temp_root_ = std::move(path); }
  std::filesystem::path TempPath() { if (!current_temp_path_.empty()) { auto path = each_temp_paths_.try_emplace(current_test_name_, current_temp_path_).first->second; EnsureTempPath(path); return path; } return TempPathFor(current_test_name_); }
  static std::optional<int> MakeTimeout() { return std::nullopt; }
  template <typename T> static std::optional<int> MakeTimeout(T value) { return std::optional<int>{static_cast<int>(value)}; }
  static bool IsMainProcess() { static const pid_t cached_main_pid = []() { if (const char* env_pid = getenv(MyTest::kMainPidEnv)) { return static_cast<pid_t>(std::strtol(env_pid, nullptr, 10)); } return getpid(); }(); return getpid() == cached_main_pid; }
  static const char* GetCurrentTestName() { return current_test_name_.c_str(); }
  bool ShouldRunInProcess(const std::string& name) const { return process_tests_.count(name) > 0; }
  bool IsIsolated(std::optional<std::string> maybe_name = std::nullopt) const { return job_isolation_ || (maybe_name ? ShouldRunInProcess(*maybe_name) : false); }
  int GetTestTimeout(const std::string& name) const { auto it = test_timeouts_.find(name); return it != test_timeouts_.end() ? it->second : timeout_; }
  static bool GetExecutablePath(char* path, size_t size) {
#ifdef __APPLE__
    char resolved[PATH_MAX]; uint32_t bufsize = static_cast<uint32_t>(size); return _NSGetExecutablePath(path, &bufsize) == 0 && realpath(path, resolved) && snprintf(path, size, "%s", resolved) > 0;
#else
    ssize_t len = readlink("/proc/self/exe", path, size - 1); return len > 0 && (path[len] = '\0', true);
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
      setenv(kMainPidEnv, std::to_string(getpid()).c_str(), 1);
      char cwd_buf[PATH_MAX];
      if (getcwd(cwd_buf, sizeof(cwd_buf))) setenv(kInitialCwdEnv, cwd_buf, 1);
      CleanupStaleTempRoots();
    }

    bool is_spawned = false, use_report = false, list_tests = false;
    current_temp_path_.clear();
    test_results_.clear();
    std::vector<std::regex> include_patterns;
    std::string report_output_path;

    // clang-format off
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i], next = (i + 1 < argc) ? argv[i + 1] : ""; auto consume_next = [&]() { i++; return next; };
      if (arg == "-p" && !next.empty()) { try { std::string p = consume_next(); (p[0] == '-' ? exclude_patterns_ : include_patterns).emplace_back(p[0] == '-' ? p.substr(1) : p); } catch (const std::exception& e) { std::cerr << e.what() << std::endl; return 1; } }
      else if (arg == "-t" && !next.empty()) timeout_ = std::stoi(consume_next());
      else if (arg == "-c") use_color_ = false;
      else if (arg == "-s") silent_ = true;
      else if (arg == "-f") force_ = true;
      else if (arg == "-j") job_isolation_ = true;
      else if (arg == "-l") list_tests = true;
      else if (arg == kSpawnedArg) is_spawned = true;
      else if (arg == kTempPathArg && !next.empty()) current_temp_path_ = consume_next();
      else if (arg == "-r") { if (!reporter_) { std::cerr << "No reporter registered.\n"; return 1; } use_report = true; report_output_path = (!next.empty() && next[0] != '-') ? consume_next() : ""; }
      else if (arg == "-h" || arg == "--help") return PrintUsage(argv[0]);
    }
    if (is_spawned) { if (const char* cwd_env = getenv(kInitialCwdEnv)) { std::ignore = chdir(cwd_env); } }
    auto IsTestSelected = [this, &include_patterns](const std::string& name) {
      auto matches = [&](const auto& p) { return std::regex_search(name, p); };
      if (std::any_of(exclude_patterns_.begin(), exclude_patterns_.end(), matches)) return false;
      return include_patterns.empty() || std::any_of(include_patterns.begin(), include_patterns.end(), matches);
    };
    // clang-format on

    // Run tests
    int num_success = 0, num_failure = 0, num_skipped = 0;
    int num_ran_tests = 0, num_filtered_tests = 0;
    colors_ = {use_color_ ? "\033[0m" : "", use_color_ ? "\033[32m" : "",
               use_color_ ? "\033[31m" : "", use_color_ ? "\033[33m" : ""};
    const char** colors = colors_.data();

    // Categorize tests while preserving registration order of groups.
    std::unordered_map<std::string, std::vector<TestPair>> categorized_tests;
    std::unordered_map<std::string, bool> group_has_normal;
    std::vector<std::string> group_order;
    for (const auto& test_pair : tests_) {
      const std::string& name = test_pair.first;
      if (!IsTestSelected(name)) continue;
      num_filtered_tests++;
      auto group_name = name.substr(0, name.find(':'));
      if (!categorized_tests.count(group_name)) group_order.push_back(group_name);
      categorized_tests[group_name].push_back(test_pair);
      if (process_tests_.count(name) == 0) group_has_normal[group_name] = true;
    }
    // clang-format off
    if (list_tests) {
      for (const auto& group_name : group_order) for (const auto& test_pair : categorized_tests[group_name]) printf("%s\n", test_pair.first.c_str());
      return 0;
    }
    if (!is_spawned)     printf("%s[==========]%s Running %d test case(s).\n", colors[GREEN], colors[RESET], num_filtered_tests);
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
    auto RunHook = [this](const auto& hooks, const std::string& name) {
      if (!name.empty() && hooks.count(name)) hooks.at(name)();
    };
    auto ResultOf = [this,
                     &colors](std::function<void()> test) -> std::tuple<bool, bool, std::string> {
      std::string message;
      bool failure = false, skipped = false;
      condition_passed_ = true, expect_failure_ = false;  // reset

      try {
        auto _ = OnScopeLeave::create([this, &failure]() {
          if (!condition_passed_) failure = true;
          SilenceOutput(false);
        });
        SilenceOutput(silent_);
        test();
      } catch (const TestSkipException& e) {
        skipped = true, message = e.what();
        printf("\n   %s\n", e.what());
      } catch (const TestAssertException& e) {
        failure = true, message = e.what();
        // the failure message is already printed.
      } catch (const TestTimeoutException& e) {
        failure = true, message = e.what();
        if (locations_.count(current_test_name_)) message += " " + locations_[current_test_name_];
        printf("\n %s\n", message.c_str());
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
        printf(failure ? "    Failed : Expected fail but passed.\n"
                       : "    Passed : Expected fail and failed.\n");
      }
      return {failure, skipped, message};
    };

    auto RunNormalTest = [this, &colors, &ResultOf, &RunHook](
                             const std::string& name, const TestFunction& test,
                             const std::string& group_name) -> ExecResult {
      result_details_.clear();
      auto temp_cleanup = OnScopeLeave::create([this, name]() { CleanupTempPath(name); });
      bool should_call_after_hook = false;
      auto [failure, skipped, message] = ResultOf([&]() {
        auto post_task_runner = OnScopeLeave::create([this]() { RunPostTestTask(); });
        RunHook(test_before_each_, group_name);
        should_call_after_hook = true;
        test();
      });

      if (should_call_after_hook) {
        auto [after_failure, _1, _2] = ResultOf([&]() { RunHook(test_after_each_, group_name); });
        failure = failure || after_failure;
      }

      if (failure && message.empty()) message = "See console output.";
      if (skipped && message.empty()) message = "Skipped.";
      return {failure, skipped, message, result_details_};
    };

    // clang-format off
    auto CaptureFailureLine = [&Clean](std::string_view line, std::vector<std::string>& details) {
      if (size_t pos = line.find(" failed ("); pos != std::string_view::npos && line.find(')', pos) != std::string_view::npos) {
        details.push_back(Clean(std::string(line)));
      }
    };
    auto ReadPipe = [&CaptureFailureLine](int fd, std::string& pending_line, std::vector<std::string>& details) {
      char buf[4096];
      for (ssize_t n; (n = read(fd, buf, sizeof(buf))) > 0;) {
        fwrite(buf, 1, n, stdout); pending_line.append(buf, n); size_t begin = 0;
        for (size_t end; (end = pending_line.find('\n', begin)) != std::string::npos; begin = end + 1) { CaptureFailureLine(std::string_view(pending_line).substr(begin, end - begin), details); }
        if (begin > 0) pending_line.erase(0, begin);
      }
    };
    // clang-format on

    auto MakeExecResult = [this, &colors](const std::string& name, int exit_code,
                                          std::vector<std::string> details) -> ExecResult {
      bool failure = false, skipped = false;
      std::string message;
      if (exit_code == -2) {
        failure = true;  // Signal timeout is always a failure
        message = "Timed out : " + name;
      } else if (exit_code > 128 && exit_code < 255) {
        failure = true;  // Signal exits: 128 + signal_number (e.g., 139 for SIGSEGV)
        int sig = exit_code - 128;
        const char* sig_name = strsignal(sig);
        message = "Terminated by signal " + std::to_string(sig) +
                  (sig_name ? (" (" + std::string(sig_name) + ")") : "");
        if (locations_.count(name)) message += " " + locations_[name];
        printf("\n%s%s%s\n", colors[RED], message.c_str(), colors[RESET]);
      } else if (exit_code >= 0 && exit_code < 16) {
        // bit 0 (1): FAILURE, bit 1 (2): SKIPPED, bit 2 (4): TIMEOUT, bit 3 (8): EXPECT_FAILURE
        failure = exit_code & 1;
        bool is_skipped = exit_code & 2;
        bool is_timeout = exit_code & 4;
        bool is_expect_failure = exit_code & 8;
        if (is_skipped) skipped = true, printf("\n   Skipped : Expected skipped.\n");
        if (is_timeout) {
          message = "Timed out : " + name;
          if (locations_.count(name)) message += " " + locations_[name];
          printf("\n %s\n", message.c_str());
        }
        if (is_expect_failure)
          printf(failure ? "    Failed : Expected fail but passed.\n"
                         : "    Passed : Expected fail and failed.\n");
        if (failure && message.empty()) message = "See console output.";
        if (skipped && message.empty()) message = "Skipped.";
      } else {
        failure = true, message = "Unknown exit code or crash: " + std::to_string(exit_code);
        if (locations_.count(name)) message += " " + locations_[name];
        printf("\n%s%s%s\n", colors[RED], message.c_str(), colors[RESET]);
      }
      return {failure, skipped, message, details};
    };

    auto RunIsolatedTestFromParent = [this, &CaptureFailureLine, &ReadPipe, &MakeExecResult](
                                         const std::string& name, const TestFunction& test,
                                         const std::string group_name = "") -> ExecResult {
      // 1. Prepare child inputs: temp path is assigned without creating it.
      const std::filesystem::path temp_path = TempPathFor(name, false);
      auto temp_cleanup = OnScopeLeave::create([this, name]() { CleanupTempPath(name); });
      char exe_path[PATH_MAX];
      if (!GetExecutablePath(exe_path, sizeof(exe_path))) {
        return {true, false, "Failed to resolve executable path", {}};
      }
      std::string timeout = std::to_string(GetTestTimeout(name));
      std::string pattern = name + "$";
      std::string temp_path_arg = temp_path.string();
      std::vector<const char*> argv_vec = {
          exe_path,        "-p",        pattern.c_str(), "-t",
          timeout.c_str(), kSpawnedArg, kTempPathArg,    temp_path_arg.c_str()};
      if (!use_color_) argv_vec.push_back("-c");
      if (silent_) argv_vec.push_back("-s");
      if (job_isolation_) argv_vec.push_back("-j");
      argv_vec.push_back(nullptr);
      // 2. Create a pipe for the child's stdout/stderr.
      int pipe_fd[2];
      if (pipe(pipe_fd) == -1) return {true, false, "Failed to create pipe", {}};
      posix_spawn_file_actions_t actions;
      posix_spawn_file_actions_init(&actions);
      posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDOUT_FILENO);
      posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDERR_FILENO);
      posix_spawn_file_actions_addclose(&actions, pipe_fd[0]);
      posix_spawn_file_actions_addclose(&actions, pipe_fd[1]);
      // 3. Spawn the isolated child process.
      pid_t worker_pid = -1;
      int spawn_ret = posix_spawn(&worker_pid, exe_path, &actions, nullptr,
                                  const_cast<char* const*>(argv_vec.data()), environ);
      posix_spawn_file_actions_destroy(&actions);
      close(pipe_fd[1]);  // Parent closes write end
      if (spawn_ret != 0) {
        close(pipe_fd[0]);
        return {true, false, "Failed to spawn worker process", {}};
      }
      // 4. Wait for the child, pipe its output, and collect failure summary lines.
      std::string pending_line;
      std::vector<std::string> details;
      fcntl(pipe_fd[0], F_SETFL, fcntl(pipe_fd[0], F_GETFL) | O_NONBLOCK);
      int status = 0, exit_code = -1;
      auto start_time = std::chrono::steady_clock::now();
      auto parent_timeout = std::chrono::milliseconds(GetTestTimeout(name) + 200);
      while (waitpid(worker_pid, &status, WNOHANG) == 0) {  // Non-blocking wait to capture pipe
        ReadPipe(pipe_fd[0], pending_line, details);        // Read available output from pipe
        if (std::chrono::steady_clock::now() - start_time > parent_timeout) {  // Check timeout
          kill(worker_pid, SIGKILL);
          waitpid(worker_pid, &status, 0);
          exit_code = -2;  // Custom timeout code
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (exit_code != -2) {
        exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
      }
      ReadPipe(pipe_fd[0], pending_line, details);  // Read remaining output
      close(pipe_fd[0]);
      if (!pending_line.empty()) { CaptureFailureLine(std::string_view(pending_line), details); }
      return MakeExecResult(name, exit_code, std::move(details));
    };

    enum ExitFlag { SUCCESS = 0, FAILURE = 1, SKIPPED = 2, TIMEOUT = 4, EXPECT_FAILURE = 8 };

    auto RunIsolatedTestInChild =
        [this, &RunHook](const std::string& name, const TestFunction& test,
                         const std::string& group_name) -> std::tuple<bool, bool, bool, bool> {
      condition_passed_ = true, expect_failure_ = false, current_test_name_ = name;
      bool failure = false, skipped = false, timeout = false;
      try {
        auto _ = OnScopeLeave::create([&, this]() { SilenceOutput(false); });
        SilenceOutput(silent_);
        RunHook(test_before_, group_name);
        RunHook(test_before_each_, group_name);
        {
          auto post_runner = OnScopeLeave::create([this]() { RunPostTestTask(); });
          test();
        }
        failure = !condition_passed_;
        RunHook(test_after_each_, group_name);
        RunHook(test_after_, group_name);
      } catch (const TestSkipException& e) {
        skipped = true;
      } catch (const TestTimeoutException& e) {
        failure = true;
        timeout = true;
      } catch (...) { failure = true; }
      if (expect_failure_) failure = !failure;
      return {failure, skipped, timeout, expect_failure_};
    };

    if (is_spawned) {
      for (const auto& test_pair : tests_) {
        const std::string& name = test_pair.first;
        if (!IsTestSelected(name)) continue;
        const auto group_name = name.substr(0, name.find(':'));
        auto [failure, skipped, timeout, expect_fail] =
            RunIsolatedTestInChild(name, test_pair.second, group_name);
        int code = SUCCESS;
        if (failure) code |= FAILURE;
        if (skipped) code |= SKIPPED;
        if (timeout) code |= TIMEOUT;
        if (expect_fail) code |= EXPECT_FAILURE;
        return code;
      }
      return FAILURE;
    }

    auto TestDispatcher = [this, &RunNormalTest, &RunIsolatedTestFromParent](
                              const std::string& name, const TestFunction& test,
                              const std::string group_name = "",
                              bool run_in_process = false) -> ExecResult {
      condition_passed_ = true, expect_failure_ = false, current_test_name_ = name;
      return run_in_process ? RunIsolatedTestFromParent(name, test, group_name)
                            : RunNormalTest(name, test, group_name);
    };

    auto RunTestCase = [&](const std::string& name, const TestFunction& test,
                           const std::string& group_name) {
      auto [failure, skipped, message, details] =
          TestDispatcher(name, test, group_name, IsIsolated(name));
      (failure ? ++num_failure : (skipped ? ++num_skipped : ++num_success));
      ++num_ran_tests;
      if (failure && !expect_failure_) printf("\n");
      PrintEnd(failure, skipped, name);
      if (auto colon = name.find(':'); colon != std::string::npos) {
        const std::string suite = name.substr(0, colon), test_name = name.substr(colon + 1);
        test_results_.push_back(
            {suite, test_name, failure, skipped, std::move(message), std::move(details)});
      }
      return failure;
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
        if (failure) num_failure++;
      }

      for (const auto& group_test : group_tests) {
        const std::string& name = group_test.first;
        const TestFunction& test = group_test.second;
        PrintStart(name);
        group_failure = RunTestCase(name, test, group_name) || group_failure;
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
        if (r.details.empty() && !r.message.empty()) printf("  - %s\n", Clean(r.message).c_str());
      }
    }
    if (reporter_ && use_report) {
      ReportSummary summary{num_ran_tests, num_failure, num_skipped};
      ReportOptions options;
      options.output_path = report_output_path;
      reporter_->OnComplete(test_results_, summary, options);
    }
    if (IsMainProcess()) {
      std::error_code ec;
      const auto temp_root = TempRoot();
      std::filesystem::remove(temp_root, ec);                              // temp root
      std::filesystem::remove(temp_root.parent_path(), ec);                // .mytest/tmp
      std::filesystem::remove(temp_root.parent_path().parent_path(), ec);  // .mytest
    }
    return num_failure > 0 ? 1 : 0;
  }

  class OnScopeLeave {
   public:
    // clang-format off
    using Function = std::function<void()>;
    OnScopeLeave(const OnScopeLeave& other) = delete;
    OnScopeLeave& operator=(const OnScopeLeave& other) = delete;
    explicit OnScopeLeave(Function&& function) : function_(std::move(function)) {}
    OnScopeLeave(OnScopeLeave&& other) : function_(std::move(other.function_)) { other.function_ = nullptr; }
    ~OnScopeLeave() { if (function_) function_(); }
    [[nodiscard]] static OnScopeLeave create(Function&& function) { return OnScopeLeave(std::move(function)); }
    // clang-format on
   private:
    Function function_;
  };

  static sigjmp_buf& timeout_jmp_buf() { return timeout_jmp_buf_; }
  static void TimeoutHandler(int signum) {
    MyTest::TimeoutScope::Reset();
    siglongjmp(MyTest::timeout_jmp_buf(), 1);
  }

  class TimeoutScope {
   public:
    TimeoutScope(int timeout_ms, void (*handler)(int)) {
      if (in_scope_.exchange(true)) throw std::runtime_error("Nested TimeoutScope not allowed");
      try {
        // SIGALRM/siglongjmp timeout: bypasses stack unwinding (No destructors called on timeout).
        if (sigaction(SIGALRM, nullptr, &old_sa_) < 0 || getitimer(ITIMER_REAL, &old_timer_) < 0) {
          throw std::runtime_error("Failed to save state");
        }
        struct sigaction sa {};
        sa.sa_handler = handler;
        sa.sa_flags = SA_RESTART;
        if (sigaction(SIGALRM, &sa, nullptr) < 0) throw std::runtime_error("Failed: sigaction");
        struct itimerval timer {
          {0, 0}, { timeout_ms / 1000, (timeout_ms % 1000) * 1000 }
        };
        if (setitimer(ITIMER_REAL, &timer, nullptr) < 0) throw std::runtime_error("Failed: itimer");
      } catch (...) {
        Reset();
        throw;
      }
    }
    ~TimeoutScope() { Reset(); }
    static void Reset() {
      if (!in_scope_.exchange(false)) return;
      struct itimerval zero_timer {};
      setitimer(ITIMER_REAL, &zero_timer, nullptr);
      setitimer(ITIMER_REAL, &old_timer_, nullptr);
      sigaction(SIGALRM, &old_sa_, nullptr);
    }

   private:
    inline static struct sigaction old_sa_;
    inline static struct itimerval old_timer_;
    inline static std::atomic<bool> in_scope_{false};
  };

 private:
  static constexpr int kDefaultTimeoutMS = 60000;
  static constexpr const char* kMainPidEnv = "MYTEST_MAIN_PID";
  static constexpr const char* kInitialCwdEnv = "MYTEST_INITIAL_CWD";
  static constexpr const char* kSpawnedArg = "--internal-spawned";
  static constexpr const char* kTempPathArg = "--internal-temp-path";
  static constexpr const char* kCalVersion = "26.06.27";
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
      << "  -l            : List selected tests without running them\n"
      << "  -s            : Silent mode (suppress stdout and stderr output)\n"
      << "  -r [FILE]     : Write report via registered reporter (optional FILE)\n"
      << "  -h, --help    : Show this help message\n\n"
      << "Tests executed by the integrated testing utility, MyTest (v" << kCalVersion << ")\n";
    return 0;
  }
  void RunPostTestTask() { for (const auto& task : post_test_tasks_) task();post_test_tasks_.clear(); }
  // Temp path rule: <cwd>/.mytest/tmp/<instance-id>/test-<temp-id>
  std::filesystem::path TempRoot() const { const char* cwd = getenv(kInitialCwdEnv); const char* main_pid = getenv(kMainPidEnv); return (temp_root_.empty() ? (cwd ? std::filesystem::path(cwd) : std::filesystem::current_path()) : temp_root_) / ".mytest" / "tmp" / (main_pid ? main_pid : std::to_string(getpid())); }
  void EnsureTempPath(const std::filesystem::path& path) { std::error_code ec; std::filesystem::create_directories(path, ec); if (ec) throw std::runtime_error("Failed to create temp path: " + path.string()); }
  std::filesystem::path TempPathFor(const std::string& key, bool create = true) { if (auto it = each_temp_paths_.find(key); it != each_temp_paths_.end()) { if (create) EnsureTempPath(it->second); return it->second; } std::ostringstream temp_id; temp_id << "test-" << std::setw(3) << std::setfill('0') << ++temp_path_sequence_; const std::filesystem::path path = TempRoot() / temp_id.str(); if (create) EnsureTempPath(path); return each_temp_paths_[key] = path; }
  void CleanupTempPath(const std::string& key) { std::error_code ec; if (auto it = each_temp_paths_.find(key); it != each_temp_paths_.end()) { std::filesystem::remove_all(it->second, ec); each_temp_paths_.erase(it); } }
  void CleanupStaleTempRoots() { std::error_code ec; const auto temp_home = TempRoot().parent_path(); if (std::filesystem::is_directory(temp_home, ec)) { for (const auto& entry : std::filesystem::directory_iterator(temp_home, ec)) { if (!entry.is_directory(ec)) continue; const auto name = entry.path().filename().string(); char* end = nullptr; const auto pid = static_cast<pid_t>(std::strtol(name.c_str(), &end, 10)); if (*end == '\0' && pid > 0 && kill(pid, 0) == -1 && errno == ESRCH) { std::filesystem::remove_all(entry.path(), ec); } } } }
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
  std::unordered_map<std::string, std::filesystem::path> each_temp_paths_;
  unsigned long long temp_path_sequence_{0};
  std::filesystem::path current_temp_path_;
  std::vector<std::string> result_details_;
  std::shared_ptr<mytest::Reporter> reporter_;
  std::filesystem::path temp_root_;

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
  static void group##name##_impl();                                                                \
  static void group##name(int timeout_ms = MyTest::Instance().timeout()) {                         \
    if (sigsetjmp(MyTest::timeout_jmp_buf(), 1) == 0) {                                            \
      MyTest::TimeoutScope timeout(timeout_ms, MyTest::TimeoutHandler);                            \
      group##name##_impl();                                                                        \
    } else {                                                                                       \
      throw MyTest::TestTimeoutException(#group ":" #name);                                        \
    }                                                                                              \
  }                                                                                                \
  static struct group##name##_Register {                                                           \
    group##name##_Register() {                                                                     \
      MyTest::Instance().RegisterTest(                                                             \
          #group ":" #name, []() { return group##name(__VA_ARGS__); },                             \
          MyTest::Instance().MakeTimeout(__VA_ARGS__), _LOC);                                      \
      if (force_process) { MyTest::Instance().RegisterProcessTest(#group ":" #name); }             \
    }                                                                                              \
  } group##name##_register;

#define TEST(group, name, ...)                                                                     \
  TEST_INTERNAL(false, group, name, __VA_ARGS__)                                                   \
  static void group##name##_impl()

#define TEST_ISOLATE(group, name, ...)                                                             \
  TEST_INTERNAL(true, group, name, __VA_ARGS__)                                                    \
  static void group##name##_impl()

#define TEST_BEFORE_EACH(group)                                                                    \
  static void group##_BeforeEach();                                                                \
  static struct group##_BeforeEach_Register {                                                      \
    group##_BeforeEach_Register() {                                                                \
      MyTest::Instance().RegisterTestBeforeEach(#group, group##_BeforeEach);                       \
    }                                                                                              \
  } group##_BeforeEach_register;                                                                   \
  static void group##_BeforeEach()

#define TEST_AFTER_EACH(group)                                                                     \
  static void group##_AfterEach();                                                                 \
  static struct group##_AfterEach_Register {                                                       \
    group##_AfterEach_Register() {                                                                 \
      MyTest::Instance().RegisterTestAfterEach(#group, group##_AfterEach);                         \
    }                                                                                              \
  } group##_AfterEach_register;                                                                    \
  static void group##_AfterEach()

#define TEST_BEFORE(group)                                                                         \
  static void group##_Before();                                                                    \
  static struct group##_Before_Register {                                                          \
    group##_Before_Register() { MyTest::Instance().RegisterTestBefore(#group, group##_Before); }   \
  } group##_Before_register;                                                                       \
  static void group##_Before()

#define TEST_AFTER(group)                                                                          \
  static void group##_After();                                                                     \
  static struct group##_After_Register {                                                           \
    group##_After_Register() { MyTest::Instance().RegisterTestAfter(#group, group##_After); }      \
  } group##_After_register;                                                                        \
  static void group##_After()

#define TEST_AFTER_ALL(group)                                                                      \
  static void group##_AfterAll();                                                                  \
  static struct group##_AfterAll_Register {                                                        \
    group##_AfterAll_Register() {                                                                  \
      MyTest::Instance().RegisterTestAfterAll(#group, group##_AfterAll);                           \
    }                                                                                              \
  } group##_AfterAll_register;                                                                     \
  static void group##_AfterAll()

#define TEST_SKIP(message)                                                                         \
  do {                                                                                             \
    if (!MyTest::Instance().force()) { throw MyTest::TestSkipException(message); }                 \
  } while (0)

#define TEST_EXPECT_FAILURE(message)                                                               \
  do { MyTest::Instance().MarkExpectFailure(true); } while (0)

#define SET_REPORTER(Reporter) MyTest::Instance().SetReporter(std::make_shared<Reporter>());
#define RUN_ALL_TESTS(argc, argv) MyTest::Instance().RunAllTests(argc, argv)
#define TEST_NAME() MyTest::Instance().GetCurrentTestName()
#define TEST_TEMP_PATH() MyTest::Instance().TempPath()

#ifdef MYTEST_CONFIG_USE_MAIN
int main(int argc, char* argv[]) { return RUN_ALL_TESTS(argc, argv); }
#endif

#define _LOC                                                                                       \
  "(" + (std::string((strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__))) + ":" +    \
      std::to_string(__LINE__) + ")"

#define _CHECK_OP(x, op, y, message, should_throw)                                                 \
  do {                                                                                             \
    [&](auto&& left, auto&& right) {                                                               \
      if (left op right) return;                                                                   \
      std::stringstream __ss;                                                                      \
      __ss << message << std::string(" ") + _LOC << "\n";                                          \
      __ss << "  Expected : (" << #x << " " #op " " << #y << ")\n";                                \
      __ss << "    Actual : (" << mytest::FormatValue(left) << " " #op " "                         \
           << mytest::FormatValue(right) << ")";                                                   \
      MyTest::Instance().PrintCheckResult(__ss.str());                                             \
      if (should_throw) throw MyTest::TestAssertException(__ss.str());                             \
      MyTest::Instance().MarkConditionPassed(false);                                               \
    }((x), (y));                                                                                   \
  } while (0)

#define EXPECT_EQ(x, y) _CHECK_OP(x, ==, y, "EXPECT_EQ failed", false)
#define EXPECT_NE(x, y) _CHECK_OP(x, !=, y, "EXPECT_NE failed", false)
#define ASSERT_EQ(x, y) _CHECK_OP(x, ==, y, "ASSERT_EQ failed", true)
#define ASSERT_NE(x, y) _CHECK_OP(x, !=, y, "ASSERT_NE failed", true)

#define EXPECT(cond) EXPECT_EQ(static_cast<bool>(cond), true)
#define ASSERT(cond) ASSERT_EQ(static_cast<bool>(cond), true)
