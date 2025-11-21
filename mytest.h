/*
 * MyTest - Lean, hassle-free testing utility, my way.
 * Copyright 2024-present Daeyeon Jeong (daeyeon.dev@gmail.com)
 *
 * Licensed under the Apache License, Version 2.0.
 * See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#pragma once

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>  // dup, dup2, fileno, close
#include <cerrno>
#include <chrono>
#include <csignal>
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

  int global;

  TEST(TestSuite, SyncTest) {
    ASSERT_EQ(1, global);
  }

  TEST(TestSuite, SyncTestTimeout, 1000) {
    TEST_EXPECT_FAILURE();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ASSERT_EQ(1, global);
  }

  TEST0(TestSuite, SyncTestOnCurrentThread) {   // No timeout support in TEST0.
    // Runs on the thread executing the test; other tests run on separate ones.
    ASSERT_EQ(1, global);
  }

  TEST_ASYNC(TestSuite, ASyncTest) {
    std::async(std::launch::async, [&done]() {
      ASSERT_EQ(1, global);
      done();      // Call `done()` passed as a parameter when async completes.
    }).get();
  }

  TEST_ASYNC(TestSuite, ASyncTestTimeout, 1000) {
    TEST_EXPECT_FAILURE();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ASSERT_EQ(1, global);
    done();
  }

  TEST_ASYNC(TestSuite, ASyncTestSkip) {
    TEST_SKIP();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ASSERT_EQ(1, global);
    done();
  }

  TEST_PROCESS(ProcSuite, RunsInFork) {
    global = 2; // Child uses its own copy; parent/global state stays untouched.
    ASSERT_EQ(global, 2);
  }

  TEST_BEFORE_EACH(TestSuite) {
    std::cout << "Before each TestSuite test" << std::endl;
  }

  TEST_AFTER_EACH(TestSuite) {
    std::cout << "After each TestSuite test" << std::endl;
  }

  TEST_BEFORE(TestSuite) {
    std::cout << "Runs once before all TestSuite tests" << std::endl;
    global = 1;
  }

  TEST_AFTER(TestSuite) {
    std::cout << "Runs once after all TestSuite tests" << std::endl;
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
   TEST_EXCLUDE           : Exclude a test from execution.
   MYTEST_CONFIG_USE_MAIN : Define to use this utility's main function.
*/

namespace mytest {
struct TestResult {
  std::string suite;
  std::string name;
  bool failure = false;
  bool skipped = false;
  std::string message;
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
    explicit TestSkipException(const std::string& msg = "Expected skipped.")
        : std::runtime_error("   Skipped : " + msg) {}
  };

  class TestTimeoutException : public std::runtime_error {
   public:
    explicit TestTimeoutException(const std::string& msg)
        : std::runtime_error(" Timed out : " + msg) {}
  };

  class TestAssertException : public std::runtime_error {
   public:
    explicit TestAssertException(const std::string& msg)
        : std::runtime_error(msg) {}
  };

  void PrintTestExpect(const std::string& msg) {
    bool prev_silent = silent_;
    if (prev_silent) SilenceOutput(false);
    std::cout << std::endl
              << colors(expect_failure_ ? RESET : RED) << msg << colors(RESET)
              << std::endl;
    if (prev_silent) SilenceOutput(true);
  }

#if !defined(UNUSED) && defined(__GNUC__)
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

  void SilenceOutput(bool silent) {
    static int stdout_backup = -1;
    static int stderr_backup = -1;
    if (silent) {
      fflush(stdout);
      fflush(stderr);
      stdout_backup = dup(fileno(stdout));
      stderr_backup = dup(fileno(stderr));
      UNUSED FILE* fp1 = freopen("/dev/null", "w", stdout);
      UNUSED FILE* fp2 = freopen("/dev/null", "w", stderr);
    } else {
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
    silent_ = silent;
  }

  // clang-format off
  void RegisterTest(const std::string& group_name, std::function<void()> test, std::optional<int> timeout = std::nullopt) { tests_.emplace_back(group_name, std::move(test)); if (timeout) test_timeouts_[group_name] = *timeout; }
  void RegisterTestBeforeEach(const std::string& group_name, std::function<void()> test) { test_before_each_[group_name] = test; }
  void RegisterTestAfterEach(const std::string& group_name, std::function<void()> test) { test_after_each_[group_name] = test; }
  void RegisterTestBefore(const std::string& group_name, std::function<void()> test) { test_before_[group_name] = test; }
  void RegisterTestAfter(const std::string& group_name, std::function<void()> test) { test_after_[group_name] = test; }
  void MarkConditionPassed(bool value) { condition_passed_ = value; }
  void MarkExpectFailure(bool value) { expect_failure_ = value; }
  void AddExcludePattern(const std::string& pattern) { exclude_patterns_.emplace_back(pattern); }
  void RegisterProcessTest(const std::string& name) { process_tests_.insert(name); }
  void SetReporter(std::shared_ptr<mytest::Reporter> reporter) { reporter_ = std::move(reporter); }
  static std::optional<int> MakeTimeout() { return std::nullopt; }
  template <typename T>
  static std::optional<int> MakeTimeout(T value) { return std::optional<int>{static_cast<int>(value)}; }
  bool ShouldRunInProcess(const std::string& name) const { return process_tests_.count(name) > 0; }
  int ResolveProcessTimeout(const std::string& name) const { auto it = test_timeouts_.find(name); return it != test_timeouts_.end() ? it->second : timeout_; }
  bool IsJobIsolated() const { return job_isolation_; }
  // clang-format on

  bool force() { return force_; }
  bool silent() { return silent_; }
  int timeout() { return timeout_; }
  bool expect_failure() { return expect_failure_; }
  enum colors { RESET, GREEN, RED, YELLOW, NUM_COLORS };
  const char* colors(size_t idx) { return colors_[idx]; }

  int RunAllTests(int argc, char* argv[]) {
    // Disable buffering for stdout and stderr
    setbuf(stdout, nullptr);
    setbuf(stderr, nullptr);

    test_results_.clear();

    // Parse CLI arguments
    bool use_color = true;
    bool silent = false;
    std::vector<std::regex> include_patterns;
    bool use_report = false;
    std::string report_output_path;

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
      } else if (arg == "-c") { use_color = false;
      } else if (arg == "-s") { silent = true;
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

    colors_ = {use_color ? "\033[0m" : "",
               use_color ? "\033[32m" : "",
               use_color ? "\033[31m" : "",
               use_color ? "\033[33m" : ""};
    const char** colors = colors_.data();

    // Categorize tests while preserving registration order of groups.
    std::unordered_map<std::string, std::vector<TestPair>> categorized_tests;
    std::vector<std::string> group_order;
    for (const auto& test_pair : tests_) {
      const std::string& name = test_pair.first;
      if (!should_run(name)) continue;

      num_filtered_tests++;
      auto group_name = name.substr(0, name.find(':'));
      if (!categorized_tests.count(group_name))
        group_order.push_back(group_name);
      categorized_tests[group_name].push_back(test_pair);
    }

    // clang-format off
                         printf("%s[==========]%s Running %d test case(s).\n", colors[GREEN], colors[RESET], num_filtered_tests);
    auto PrintStart = [&colors, this](const std::string& name) {
                         printf("%s[ RUN      ]%s %s", colors[GREEN], colors[RESET], name.c_str());
                         if (job_isolation_) printf(" (PID: %d)", getpid());
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

    auto RunTestWithHooks = [this, &colors, &silent](
                                const std::string& name,
                                const TestFunction& test,
                                const std::string& group_name)
        -> std::tuple<bool, bool, std::string> {
      bool failure = false, skipped = false;
      std::string message;
      condition_passed_ = true;
      expect_failure_ = false;

      try {
        auto _ = OnScopeLeave::create([&, this]() {
          if (!group_name.empty() && test_after_each_.count(group_name)) {
            test_after_each_[group_name]();
          }
          if (!condition_passed_) {
            failure = true;
          }
          SilenceOutput(silent && false);
        });
        SilenceOutput(silent && true);

        if (!group_name.empty() && test_before_each_.count(group_name)) {
          test_before_each_[group_name]();
        }

        test();

      } catch (const TestSkipException& e) {
        skipped = true;
        printf("\n%s\n", e.what());
        message = e.what();
      } catch (const TestAssertException& e) {
        failure = true;
        auto color = colors[expect_failure_ ? RESET : RED];
        printf("\n%s%s%s\n", color, e.what(), colors[RESET]);
        message = e.what();
      } catch (const TestTimeoutException& e) {
        failure = true;
        printf("\n%s\n", e.what());
        message = e.what();
      } catch (const std::exception& e) {
        printf("\nException : %s\n", e.what());
        failure = true;
        message = e.what();
      } catch (...) {
        printf("\nException : Unknown\n");
        failure = true;
        message = "Unknown exception";
      }

      if (expect_failure_) {
        failure = !failure;
        if (failure) {
          printf("    Failed : Expected fail but passed.\n");
        } else {
          printf("    Passed : Expected fail and failed.\n");
        }
      }

      if (failure && message.empty()) message = "See console output.";
      if (skipped && message.empty()) message = "Skipped.";
      return std::make_tuple(failure, skipped, message);
    };

    auto RunTestInProcess = [this, &silent, &RunTestWithHooks](
                                const std::string& name,
                                const TestFunction& test,
                                const std::string group_name =
                                    "") -> std::tuple<bool, bool, std::string> {
      int pipe_fds[2];
      if (pipe(pipe_fds) == -1) {
        printf("\nFailed to create pipe for %s\n", name.c_str());
        auto [failure, skipped, message] =
            RunTestWithHooks(name, test, group_name);
        return std::make_tuple(failure, skipped, message);
      }

      pid_t pid = fork();
      if (pid < 0) {
        printf("\nFailed to fork process for %s\n", name.c_str());
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        auto [failure, skipped, message] =
            RunTestWithHooks(name, test, group_name);
        return std::make_tuple(failure, skipped, message);
      }

      if (pid == 0) {
        // Child: redirect stdout/stderr to the pipe and run the test body.
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        auto [failure, skipped, message] =
            RunTestWithHooks(name, test, group_name);
        (void)message;
        fflush(stdout);
        fflush(stderr);
        _exit(skipped ? 2 : failure ? 1 : 0);
      }
      // Parent: monitor output and exit status without blocking the main loop.
      close(pipe_fds[1]);
      (void)fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK);

      std::string captured_output;
      int status = 0;
      bool timed_out = false;
      bool pipe_open = true;
      bool child_alive = true;
      const int process_timeout = ResolveProcessTimeout(name);
      const auto deadline =
          process_timeout > 0
              ? std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(process_timeout + 500)
              : std::chrono::steady_clock::time_point::max();

      while (pipe_open || child_alive) {
        if (pipe_open) {
          char buffer[4096];
          ssize_t count = read(pipe_fds[0], buffer, sizeof(buffer));
          if (count > 0) {
            if (!silent) {
              std::cout.write(buffer, count);
              std::cout.flush();
            }
            captured_output.append(buffer, static_cast<size_t>(count));
          } else if (count == 0) {
            pipe_open = false;
          } else if (!(errno == EAGAIN || errno == EINTR)) {
            pipe_open = false;
          }
        }

        // Wait for the child to exit, but keep the loop responsive (WNOHANG).
        if (child_alive) {
          pid_t res = waitpid(pid, &status, WNOHANG);
          if (res == pid) {
            child_alive = false;
          } else if (res == 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
              timed_out = true;
              kill(pid, SIGKILL);
              waitpid(pid, &status, 0);
              child_alive = false;
            }
          } else if (res == -1) {
            if (errno == EINTR) {
              status = 0;
            } else {
              status = -1;
              child_alive = false;
            }
          }
        }

        if (!pipe_open && !child_alive) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }

      close(pipe_fds[0]);

      if (!silent && !captured_output.empty()) {
        if (captured_output.back() != '\n') std::cout << std::endl;
      }

      bool failure = false, skipped = false;
      std::string message;

      if (timed_out) {
        failure = true;
        printf("\nTimed out : %s\n", name.c_str());
        message = "Test timed out.";
      } else if (status == -1) {
        failure = true;
        printf("\nwaitpid failed for %s\n", name.c_str());
        message = "waitpid failed.";
      } else if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 2) {
          skipped = true;
          message = captured_output.empty() ? "Skipped." : captured_output;
        } else if (exit_code != 0) {
          failure = true;
          message =
              captured_output.empty() ? "See console output." : captured_output;
        } else if (!captured_output.empty()) {
          message = captured_output;
        }
      } else if (WIFSIGNALED(status)) {
        failure = true;
        int sig = WTERMSIG(status);
        const char* sig_name = strsignal(sig);
        printf("\nTerminated by signal %d (%s) : %s\n",
               sig,
               sig_name ? sig_name : "unknown",
               name.c_str());
        std::ostringstream oss;
        oss << "Terminated by signal " << sig;
        if (sig_name) oss << " (" << sig_name << ")";
        message = oss.str();
      } else {
        failure = true;
        message = "Unknown child status.";
      }

      if (failure && message.empty()) message = "See console output.";
      return std::make_tuple(failure, skipped, message);
    };

    auto RunTest = [this, &RunTestWithHooks, &RunTestInProcess](
                       const std::string& name,
                       const TestFunction& test,
                       const std::string group_name = "",
                       bool run_in_process =
                           false) -> std::tuple<bool, bool, std::string> {
      condition_passed_ = true;
      expect_failure_ = false;
      auto [failure, skipped, message] =
          run_in_process ? RunTestInProcess(name, test, group_name)
                         : RunTestWithHooks(name, test, group_name);
      while (!message.empty() &&
             (message.back() == '\n' || message.back() == '\r')) {
        message.pop_back();
      }
      return std::make_tuple(failure, skipped, message);
    };

    for (const auto& group_name : group_order) {
      const auto& group_tests = categorized_tests[group_name];
      bool group_failure = false;
      bool group_skipped = false;

      PrintStart(group_name);

      if (test_before_.count(group_name)) {
        auto [failure, skipped, message] =
            RunTest(group_name, test_before_[group_name]);
        (void)message;
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

        auto [failure, skipped, message] = RunTest(
            name, test, group_name, ShouldRunInProcess(name) || job_isolation_);
        (failure ? ++num_failure : (skipped ? ++num_skipped : ++num_success));
        ++num_ran_tests;

        if (failure && !expect_failure_) printf("\n");
        PrintEnd(failure, skipped, name);

        auto colon = name.find(':');
        if (colon != std::string::npos) {
          TestResult result;
          result.suite = name.substr(0, colon);
          result.name = name.substr(colon + 1);
          result.failure = failure;
          result.skipped = skipped;
          result.message = std::move(message);
          while (!result.message.empty() && (result.message.back() == '\n' ||
                                             result.message.back() == '\r')) {
            result.message.pop_back();
          }
          test_results_.push_back(std::move(result));
        }

        group_failure = group_failure || failure;
      }

      if (test_after_.count(group_name)) {
        auto [failure, skipped, message] =
            RunTest(group_name, test_after_[group_name]);
        (void)message;
        group_failure = group_failure || failure;
        if (failure) num_failure++;
      }

      PrintEnd(group_failure, group_skipped, group_name);
    }

    PrintResult();
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

 private:
  static constexpr int kDefaultTimeoutMS = 60000;
  static constexpr const char* kCalVersion = "25.11.22";

  int PrintUsage(const char* name) {
    // clang-format off
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

  int timeout_ = kDefaultTimeoutMS;
  bool force_ = false;
  bool job_isolation_ = false;
  bool silent_ = false;
  bool condition_passed_ = true;
  bool expect_failure_ = false;
  std::vector<const char*> colors_;
  std::vector<std::regex> exclude_patterns_;
  std::unordered_set<std::string> process_tests_;
  std::unordered_map<std::string, int> test_timeouts_;
  std::vector<TestResult> test_results_;
  std::shared_ptr<mytest::Reporter> reporter_;

  using TestFunction = std::function<void()>;
  using TestPair = std::pair<std::string, TestFunction>;
  std::vector<TestPair> tests_;
  std::unordered_map<std::string, std::function<void()>> test_before_each_;
  std::unordered_map<std::string, std::function<void()>> test_after_each_;
  std::unordered_map<std::string, std::function<void()>> test_before_;
  std::unordered_map<std::string, std::function<void()>> test_after_;
  MyTest() {}
  MyTest(const MyTest&) = delete;
  MyTest& operator=(const MyTest&) = delete;
};

#define TEST_INLINE(group, name)                                               \
  void group##name##_impl();                                                   \
  struct group##name##_Register {                                              \
    group##name##_Register() {                                                 \
      MyTest::Instance().RegisterTest(#group ":" #name, group##name##_impl);   \
    }                                                                          \
  } group##name##_register;                                                    \
  void group##name##_impl()

#define TEST_INTERNAL(is_sync, force_process, group, name, ...)                \
  void group##name##_impl(std::function<void()> done);                         \
  void group##name(int timeout_ms = MyTest::Instance().timeout()) {            \
    auto promise = std::make_shared<std::promise<void>>();                     \
    auto future = promise->get_future();                                       \
    auto done = [promise, &future]() {                                         \
      if (future.valid() && future.wait_for(std::chrono::seconds(0)) ==        \
                                std::future_status::timeout) {                 \
        promise->set_value();                                                  \
      }                                                                        \
    };                                                                         \
    std::thread([done, promise]() {                                            \
      try {                                                                    \
        group##name##_impl(done);                                              \
        if (is_sync) done();                                                   \
      } catch (...) {                                                          \
        promise->set_exception(std::current_exception());                      \
      }                                                                        \
    }).detach();                                                               \
    if (future.wait_for(std::chrono::milliseconds(timeout_ms)) ==              \
        std::future_status::timeout) {                                         \
      throw MyTest::TestTimeoutException(#group ":" #name);                    \
    }                                                                          \
    future.get();                                                              \
  }                                                                            \
  struct group##name##_Register {                                              \
    group##name##_Register() {                                                 \
      MyTest::Instance().RegisterTest(                                         \
          #group ":" #name,                                                    \
          []() { return group##name(__VA_ARGS__); },                           \
          MyTest::MakeTimeout(__VA_ARGS__));                                   \
      if (force_process) {                                                     \
        MyTest::Instance().RegisterProcessTest(#group ":" #name);              \
      }                                                                        \
    }                                                                          \
  } group##name##_register;

#define TEST(group, name, ...)                                                 \
  TEST_INTERNAL(true, false, group, name, __VA_ARGS__)                         \
  void group##name##_impl(std::function<void()>)

#define TEST0(group, name) TEST_INLINE(group, name)

#define TEST_PROCESS(group, name, ...)                                         \
  TEST_INTERNAL(true, true, group, name, __VA_ARGS__)                          \
  void group##name##_impl(std::function<void()>)

#define TEST_ASYNC(group, name, ...)                                           \
  TEST_INTERNAL(false, false, group, name, __VA_ARGS__)                        \
  void group##name##_impl(std::function<void()> done)

#define TEST_BEFORE_EACH(group)                                                \
  void group##_BeforeEach();                                                   \
  struct group##_BeforeEach_Register {                                         \
    group##_BeforeEach_Register() {                                            \
      MyTest::Instance().RegisterTestBeforeEach(#group, group##_BeforeEach);   \
    }                                                                          \
  } group##_BeforeEach_register;                                               \
  void group##_BeforeEach()

#define TEST_AFTER_EACH(group)                                                 \
  void group##_AfterEach();                                                    \
  struct group##_AfterEach_Register {                                          \
    group##_AfterEach_Register() {                                             \
      MyTest::Instance().RegisterTestAfterEach(#group, group##_AfterEach);     \
    }                                                                          \
  } group##_AfterEach_register;                                                \
  void group##_AfterEach()

#define TEST_BEFORE(group)                                                     \
  void group##_Before();                                                       \
  struct group##_Before_Register {                                             \
    group##_Before_Register() {                                                \
      MyTest::Instance().RegisterTestBefore(#group, group##_Before);           \
    }                                                                          \
  } group##_Before_register;                                                   \
  void group##_Before()

#define TEST_AFTER(group)                                                      \
  void group##_After();                                                        \
  struct group##_After_Register {                                              \
    group##_After_Register() {                                                 \
      MyTest::Instance().RegisterTestAfter(#group, group##_After);             \
    }                                                                          \
  } group##_After_register;                                                    \
  void group##_After()

#define TEST_SKIP(msg)                                                         \
  do {                                                                         \
    if (!MyTest::Instance().force()) {                                         \
      throw MyTest::TestSkipException(msg);                                    \
    }                                                                          \
  } while (0)

#define TEST_EXPECT_FAILURE(msg)                                               \
  do {                                                                         \
    MyTest::Instance().MarkExpectFailure(true);                                \
  } while (0)

#define _VA(_1, _2, NAME, ...) NAME
#define _TEST_EXCLUDE_ARG1(group) _TEST_EXCLUDE_ARG2(group, EMPTYSTR)
#define _TEST_EXCLUDE_ARG2(group_or_pattern, name)                             \
  struct group_or_pattern##_##name##_Add_Exclude_Pattern {                     \
    group_or_pattern##_##name##_Add_Exclude_Pattern() {                        \
      std::string pattern = #group_or_pattern;                                 \
      if (std::string(#name) != "EMPTYSTR") pattern += ":" #name;              \
      MyTest::Instance().AddExcludePattern(pattern);                           \
    }                                                                          \
  } group_or_pattern##_##name##_add_exclude_pattern;
#define TEST_EXCLUDE(...)                                                      \
  _VA(__VA_ARGS__, _TEST_EXCLUDE_ARG2, _TEST_EXCLUDE_ARG1)(__VA_ARGS__)

#define SET_REPORTER(Reporter)                                                 \
  MyTest::Instance().SetReporter(std::make_shared<Reporter>());

#define RUN_ALL_TESTS(argc, argv) MyTest::Instance().RunAllTests(argc, argv)

#ifdef MYTEST_CONFIG_USE_MAIN
int main(int argc, char* argv[]) {
  return RUN_ALL_TESTS(argc, argv);
}
#endif

#ifndef FILE_NAME_
#define FILE_NAME_                                                             \
  std::string((strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__))
#endif

#define LOC_ "(" + FILE_NAME_ + ":" + std::to_string(__LINE__) + ")"

#define FORMAT_ERROR_MESSAGE_(x, cond_str, y, msg)                             \
  std::stringstream __ss;                                                      \
  __ss << msg << std::string(" ") + LOC_ << "\n";                              \
  __ss << "  Expected : (" << #x << " " cond_str " " << #y << ")\n";           \
  __ss << "    Actual : (" << x << " " cond_str " " << y << ")";

#define CHECK_CONDITION_(cond, x, y, cond_str, msg, should_throw)              \
  do {                                                                         \
    if (cond) {                                                                \
      FORMAT_ERROR_MESSAGE_(x, y, cond_str, msg);                              \
      if (should_throw) {                                                      \
        throw MyTest::TestAssertException(__ss.str());                         \
      } else {                                                                 \
        MyTest::Instance().PrintTestExpect(__ss.str());                        \
        MyTest::Instance().MarkConditionPassed(false);                         \
      }                                                                        \
    }                                                                          \
  } while (0)

#define EXPECT_(cond, x, c, y, m) CHECK_CONDITION_(!(cond), x, #c, y, m, false)
#define ASSERT_(cond, x, c, y, m) CHECK_CONDITION_(!(cond), x, #c, y, m, true)
#define EXPECT_EQ(x, y) EXPECT_((x) == (y), x, ==, y, "EXPECT_EQ failed")
#define EXPECT_NE(x, y) EXPECT_((x) != (y), x, !=, y, "EXPECT_NE failed")
#define ASSERT_EQ(x, y) ASSERT_((x) == (y), x, ==, y, "ASSERT_EQ failed")
#define ASSERT_NE(x, y) ASSERT_((x) != (y), x, !=, y, "ASSERT_NE failed")

#define EXPECT(cond) EXPECT_EQ(static_cast<bool>(cond), true)
#define ASSERT(cond) ASSERT_EQ(static_cast<bool>(cond), true)
