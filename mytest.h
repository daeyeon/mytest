/*
 * MyTest - A header-only util for unit testing.
 * Copyright 2024-present Daeyeon Jeong (daeyeon.dev@gmail.com)
 *
 * Licensed under the Apache License, Version 2.0.
 * See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#pragma once

#include <unistd.h>  // dup, dup2, fileno, close
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
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

  TEST0(TestSuite, SyncTestOnCurrentThread) {
    // Runs on current thread; others on separate threads until timeout.
    ASSERT_EQ(1, global);
  }

  TEST_ASYNC(TestSuite, ASyncTest) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ASSERT_EQ(1, global);
    done();
  }

  TEST_ASYNC(TestSuite, ASyncTestTimeout, 1000) {
    TEST_EXPECT_FAILURE();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ASSERT_EQ(1, global);
    done();
  }

  TEST_ASYNC(TestSuite, ASyncTestSkip) {
    TEST_SKIP("Skipping this test");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ASSERT_EQ(1, global);
    done();
  }

  TEST_BEFORE_EACH(TestSuite) {
    std::cout << "Before each TestSuite test" << std::endl;
  }

  TEST_AFTER_EACH(TestSuite) {
    std::cout << "After each TestSuite test" << std::endl;
  }

  TEST_BEFORE(TestSuite) {
    std::cout << "Runs once before all TestSuite tests" << std::endl;
  }

  TEST_AFTER(TestSuite) {
    std::cout << "Runs once after all TestSuite tests" << std::endl;
  }

  int main(int argc, char* argv[]) {
    global = 1;
    return RUN_ALL_TESTS(argc, argv);
  }

  Command Line Usage Examples:
   ./your_test "TestSuite:"      // Run all tests in 'TestSuite'
   ./your_test "TestTimeout"     // Run all tests with 'TestTimeout' in the name
   ./your_test "-TestSuite:"     // Exclude all tests in 'TestSuite'
   ./your_test "-Timeout"        // Exclude all tests with 'Timeout' in the name
*/

class MyTest {
 public:
  static MyTest& Instance() {
    static MyTest instance;
    return instance;
  }

  class TestSkipException : public std::exception {
   public:
    explicit TestSkipException(const std::string msg = "")
        : msg_("   Skipped" + (msg.empty() ? "" : " : '" + msg + "'")) {}
    const char* what() const noexcept override { return msg_.c_str(); }

   private:
    std::string msg_;
  };

  class TestTimeoutException : public std::exception {
   public:
    explicit TestTimeoutException(const std::string msg = "")
        : msg_(" Timed out" + (msg.empty() ? "" : " : " + msg)) {}
    const char* what() const noexcept override { return msg_.c_str(); }

   private:
    std::string msg_;
  };

  // clang-format off
  void RegisterTest(const std::string& group_name, std::function<void()> test) {
    tests_.emplace_back(group_name, test);
  }

  void RegisterTestBeforeEach(const std::string& group_name, std::function<void()> test) {
    test_before_each_[group_name] = test;
  }

  void RegisterTestAfterEach(const std::string& group_name, std::function<void()> test) {
    test_after_each_[group_name] = test;
  }

  void RegisterTestBefore(const std::string& group_name, std::function<void()> test) {
    test_before_[group_name] = test;
  }

  void RegisterTestAfter(const std::string& group_name, std::function<void()> test) {
    test_after_[group_name] = test;
  }
  // clang-format on

  void MarkConditionPassed(bool value) { condition_passed_ = value; }

  void MarkExpectFailure(bool value) { expect_failure_ = value; }

  void SilenceOutput(bool silent) {
    static int stdout_backup = -1;
    static int stderr_backup = -1;

    if (silent) {
      fflush(stdout);
      fflush(stderr);
      stdout_backup = dup(fileno(stdout));
      stderr_backup = dup(fileno(stderr));

      freopen("/dev/null", "w", stdout);
      freopen("/dev/null", "w", stderr);
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
  }

  int RunAllTests(int argc, char* argv[]) {
    int default_timeout = default_timeout_;

    // Parse CLI arguments
    bool use_color = true;
    bool silent = false;
    std::vector<std::regex> include_patterns;
    std::vector<std::regex> exclude_patterns;

    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "-p" && i + 1 < argc) {
        std::string pattern = argv[++i];
        try {
          if (pattern[0] == '-') {
            exclude_patterns.emplace_back(pattern.substr(1));
          } else {
            include_patterns.emplace_back(pattern);
          }
        } catch (const std::exception& e) {
          std::cerr << e.what() << '\n';
          return 1;
        }
      } else if (arg == "-t" && i + 1 < argc) {
        default_timeout = std::stoi(argv[++i]);
      } else if (arg == "-c") {
        use_color = false;
      } else if (arg == "-s") {
        silent = true;
      } else if (arg == "-f") {
        force_ = true;
      } else if (arg == "-h" || arg == "--help") {
        PrintUsage(argv[0], default_timeout);
        return 0;
      }
    }
    default_timeout_ = default_timeout;

    auto should_run = [&](const std::string& name) {
      for (const auto& pattern : exclude_patterns) {
        if (std::regex_search(name, pattern)) {
          return false;
        }
      }
      if (include_patterns.empty()) {
        return true;
      }
      for (const auto& pattern : include_patterns) {
        if (std::regex_search(name, pattern)) {
          return true;
        }
      }
      return false;
    };

    // Run tests

    int num_success = 0;
    int num_failure = 0;
    int num_skipped = 0;
    int num_ran_tests = 0;

    enum colors { RESET, GREEN, RED, YELLOW };
    const char* colors[] = {use_color ? "\033[0m" : "",
                            use_color ? "\033[32m" : "",
                            use_color ? "\033[31m" : "",
                            use_color ? "\033[33m" : ""};

    printf("%s[==========]%s Running %zu test case(s).\n",
           colors[GREEN],
           colors[RESET],
           tests_.size());

    std::unordered_map<std::string, bool> group_tested;

    // Run all tests
    for (const auto& test_pair : tests_) {
      const std::string& name = test_pair.first;
      const TestFunction& test = test_pair.second;
      if (!should_run(name)) {
        continue;
      }
      bool failure = false;
      bool skipped = false;

      auto group_name = name.substr(0, name.find(':'));
      {
        auto _ = OnScopeLeave::create(
            [&silent, this]() { SilenceOutput(silent && false); });
        SilenceOutput(silent && true);
        if (!group_tested[group_name] && test_before_.count(group_name)) {
          test_before_[group_name]();
          group_tested[group_name] = true;
        }
      }

      printf(
          "%s[ RUN      ]%s %s\n", colors[GREEN], colors[RESET], name.c_str());

      try {
        failure = false;
        skipped = false;
        condition_passed_ = true;
        expect_failure_ = false;

        auto _ = OnScopeLeave::create([&, this]() {
          if (test_after_each_.count(group_name)) {
            test_after_each_[group_name]();
          }
          if (!condition_passed_) {
            failure = true;
          }
          SilenceOutput(silent && false);
        });

        SilenceOutput(silent && true);

        if (test_before_each_.count(group_name)) {
          test_before_each_[group_name]();
        }

        std::visit([](auto&& test) { test(); }, test);

      } catch (const TestSkipException& e) {
        skipped = true;
        printf("\n%s\n", e.what());
      } catch (const TestTimeoutException& e) {
        failure = true;
        printf("\n%s\n", e.what());
      } catch (const std::runtime_error& e) {
        failure = true;
        printf("\n%s\n", e.what());
      } catch (const std::exception& e) {
        printf("\nException : %s\n", e.what());
        failure = true;
      } catch (...) {
        printf("\nException : Unknown\n");
        failure = true;
      }

      if (failure && expect_failure_) {
        failure = false;
      }

      ++num_ran_tests;
      (failure ? ++num_failure : (skipped ? ++num_skipped : ++num_success));

      // clang-format off
      if (failure)      printf("%s[  FAILED  ]%s %s\n", colors[RED], colors[RESET], name.c_str());
      else if (skipped) printf("%s[  SKIPPED ]%s %s\n", colors[YELLOW], colors[RESET], name.c_str());
      else              printf("%s[       OK ]%s %s\n", colors[GREEN], colors[RESET],name.c_str());
      // clang-format on
    }

    {
      auto _ = OnScopeLeave::create(
          [&silent, this]() { SilenceOutput(silent && false); });
      SilenceOutput(silent && true);
      for (const auto& group : group_tested) {
        if (test_after_.count(group.first)) {
          test_after_[group.first]();
        }
      }
    }

    // clang-format off
                         printf("%s[==========]%s %d test case(s) ran.\n", colors[GREEN], colors[RESET], num_ran_tests);
                         printf("%s[  PASSED  ]%s %d test(s).\n", colors[GREEN], colors[RESET], num_ran_tests - num_failure - num_skipped);
    (num_skipped > 0) && printf("%s[  SKIPPED ]%s %d test(s):\n", colors[YELLOW], colors[RESET], num_skipped);
    (num_failure > 0) && printf("%s[  FAILED  ]%s %d test(s)\n", colors[RED], colors[RESET], num_failure);
    // clang-format on
    return num_failure > 0 ? 1 : 0;
  }

  int default_timeout() { return default_timeout_; }
  bool force() { return force_; }

 private:
  static constexpr int kDefaultTimeoutMS = 60000;
  static constexpr const char* kCalVersion = "25.01.0";

#if !defined(WARN_UNUSED_RESULT) && defined(__GNUC__)
#define WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))
#else
#define WARN_UNUSED_RESULT
#endif

  class OnScopeLeave {
   public:
    using Function = std::function<void()>;

    OnScopeLeave(const OnScopeLeave& other) = delete;
    OnScopeLeave& operator=(const OnScopeLeave& other) = delete;

    explicit OnScopeLeave(Function&& function)
        : function_(std::move(function)) {}
    OnScopeLeave(OnScopeLeave&& other) : function_(std::move(other.function_)) {
      other.function_ = nullptr;
    }
    ~OnScopeLeave() {
      if (function_) {
        function_();
      }
    }

    static WARN_UNUSED_RESULT OnScopeLeave create(Function&& function) {
      return OnScopeLeave(std::move(function));
    }

   private:
    Function function_;
  };

  void PrintUsage(const char* name, int default_timeout) {
    // clang-format off
    std::cout
        << "Usage: " << name << " [options]\n"
        << "Options:\n"
        << "  -p \"pattern\"  : Include tests matching the pattern\n"
        << "  -p \"-pattern\" : Exclude tests matching the pattern\n"
        << "  -t \"timeout\"  : Set the default timeout value (in milliseconds, default: " << default_timeout << ")\n"
        << "  -c            : Disable color output\n"
        << "  -f            : Force mode, run all, including skipped tests\n"
        << "  -s            : Silent mode, suppress stdout and stderr\n"
        << "  -h, --help    : Show this help message\n\n"
        << "Driven by MyTest (v" << kCalVersion << ")\n";
    // clang-format on
  }

  int default_timeout_;
  bool force_ = false;
  bool condition_passed_ = true;
  bool expect_failure_ = false;

  using TestFunction =
      std::variant<std::function<void()>, std::function<std::future<void>()>>;
  std::vector<std::pair<std::string, TestFunction>> tests_;
  std::unordered_map<std::string, std::function<void()>> test_before_each_;
  std::unordered_map<std::string, std::function<void()>> test_after_each_;
  std::unordered_map<std::string, std::function<void()>> test_before_;
  std::unordered_map<std::string, std::function<void()>> test_after_;
  MyTest() : default_timeout_(kDefaultTimeoutMS) {}
  MyTest(const MyTest&) = delete;
  MyTest& operator=(const MyTest&) = delete;
};

#define TEST0(group, name)                                                     \
  void group##name##_impl();                                                   \
  struct group##name##_Register {                                              \
    group##name##_Register() {                                                 \
      MyTest::Instance().RegisterTest(#group ":" #name, group##name##_impl);   \
    }                                                                          \
  } group##name##_register;                                                    \
  void group##name##_impl()

#define TEST(group, name, ...)                                                 \
  void group##name##_impl();                                                   \
  std::future<void> group##name(int timeout_ms =                               \
                                    MyTest::Instance().default_timeout()) {    \
    auto promise = std::make_shared<std::promise<void>>();                     \
    auto future = promise->get_future();                                       \
    std::thread([promise]() {                                                  \
      try {                                                                    \
        group##name##_impl();                                                  \
        promise->set_value();                                                  \
      } catch (...) {                                                          \
        promise->set_exception(std::current_exception());                      \
      }                                                                        \
    }).detach();                                                               \
    if (future.wait_for(std::chrono::milliseconds(timeout_ms)) ==              \
        std::future_status::timeout) {                                         \
      throw MyTest::TestTimeoutException(#group ":" #name);                    \
    }                                                                          \
    future.get();                                                              \
    return future;                                                             \
  }                                                                            \
  struct group##name##_Register {                                              \
    group##name##_Register() {                                                 \
      MyTest::Instance().RegisterTest(                                         \
          #group ":" #name, []() { return group##name(__VA_ARGS__); });        \
    }                                                                          \
  } group##name##_register;                                                    \
  void group##name##_impl()

#define TEST_ASYNC(group, name, ...)                                           \
  void group##name##_impl(std::function<void()> done);                         \
  std::future<void> group##name(int timeout_ms =                               \
                                    MyTest::Instance().default_timeout()) {    \
    auto promise = std::make_shared<std::promise<void>>();                     \
    auto future = promise->get_future();                                       \
    auto done = [promise]() { promise->set_value(); };                         \
    std::thread([done, promise]() {                                            \
      try {                                                                    \
        group##name##_impl(done);                                              \
      } catch (...) {                                                          \
        promise->set_exception(std::current_exception());                      \
      }                                                                        \
    }).detach();                                                               \
    if (future.wait_for(std::chrono::milliseconds(timeout_ms)) ==              \
        std::future_status::timeout) {                                         \
      throw MyTest::TestTimeoutException(#group ":" #name);                    \
    }                                                                          \
    future.get();                                                              \
    return future;                                                             \
  }                                                                            \
  struct group##name##_Register {                                              \
    group##name##_Register() {                                                 \
      MyTest::Instance().RegisterTest(                                         \
          #group ":" #name, []() { return group##name(__VA_ARGS__); });        \
    }                                                                          \
  } group##name##_register;                                                    \
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

#define RUN_ALL_TESTS(argc, argv) MyTest::Instance().RunAllTests(argc, argv)

#ifdef MYTEST_CONFIG_USE_MAIN
int main(int argc, char* argv[]) {
  return RUN_ALL_TESTS(argc, argv);
}
#endif

#ifndef __FILE_NAME
#define __FILE_NAME                                                            \
  std::string((strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__))
#endif

#define __LOC "(" + __FILE_NAME + ":" + std::to_string(__LINE__) + ")"

#define FORMAT_ERROR_MESSAGE(x, cond_str, y, msg)                              \
  std::stringstream ss;                                                        \
  ss << msg << std::string(" ") + __LOC << "\n";                               \
  ss << "  Expected : (" << #x << " " cond_str " " << #y << ")\n";             \
  ss << "    Actual : (" << x << " " cond_str " " << y << ")";

#define CHECK_CONDITION(cond, x, y, cond_str, msg, should_throw)               \
  if (cond) {                                                                  \
    FORMAT_ERROR_MESSAGE(x, y, cond_str, msg);                                 \
    if (should_throw) {                                                        \
      throw std::runtime_error(ss.str());                                      \
    } else {                                                                   \
      std::cout << "\n" << ss.str() << "\n";                                   \
      MyTest::Instance().MarkConditionPassed(false);                           \
    }                                                                          \
  }

#define EXPECT_(cond, x, c, y, m) CHECK_CONDITION(!(cond), x, #c, y, m, false)
#define ASSERT_(cond, x, c, y, m) CHECK_CONDITION(!(cond), x, #c, y, m, true)
#define EXPECT_EQ(x, y) EXPECT_((x) == (y), x, ==, y, "EXPECT_EQ failed")
#define EXPECT_NE(x, y) EXPECT_((x) != (y), x, !=, y, "EXPECT_NE failed")
#define ASSERT_EQ(x, y) ASSERT_((x) == (y), x, ==, y, "ASSERT_EQ failed")
#define ASSERT_NE(x, y) ASSERT_((x) != (y), x, !=, y, "ASSERT_NE failed")

#define EXPECT(cond) EXPECT_EQ(static_cast<bool>(cond), true)
#define ASSERT(cond) ASSERT_EQ(static_cast<bool>(cond), true)
