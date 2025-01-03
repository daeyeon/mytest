/*
 * MyTest - A header-only util for unit testing.
 * Copyright 2024-present Daeyeon Jeong (daeyeon.dev@gmail.com)
 *
 * Licensed under the Apache License, Version 2.0.
 * See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#pragma once

#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

/*
  Usage:

  int global;

  TEST(Subject, SyncTest) {
    ASSERT_EQ(1, global);
  }

  TEST(Subject, SyncTestTimeout, 1000) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ASSERT_EQ(1, global);
  }

  TEST0(Subject, SyncTestOnCurrentThread) {
    // Runs on current thread; others on separate threads until timeout.
    ASSERT_EQ(1, global);
  }

  TEST_ASYNC(Subject, ASyncTest) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ASSERT_EQ(1, global);
    done();
  }

  TEST_ASYNC(Subject, ASyncTestTimeout, 1000) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ASSERT_EQ(1, global);
    done();
  }

  TEST_ASYNC(Subject, ASyncTestSkip) {
    TEST_SKIP("Skipping this test");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ASSERT_EQ(1, global);
    done();
  }

  TEST_BEFORE_EACH(Subject) {
    std::cout << "Before each Subject test" << std::endl;
  }

  TEST_AFTER_EACH(Subject) {
    std::cout << "After each Subject test" << std::endl;
  }

  TEST_BEFORE(Subject) {
    std::cout << "Runs once before all Subject tests" << std::endl;
  }

  TEST_AFTER(Subject) {
    std::cout << "Runs once after all Subject tests" << std::endl;
  }

  int main(int argc, char* argv[]) {
    global = 1;
    return RUN_ALL_TESTS(argc, argv);
  }

  Command Line Usage Examples:
   ./your_test "Subject::*"  // Run all tests in Subject group
   ./your_test "Subject::SyncTestTimeout"  // Run specific test
   ./your_test "*TestTimeout*"  // Run all tests with Function in the name
   ./your_test "-Subject::*"  // Exclude all tests in Subject group
   ./your_test "-Subject::SyncTestTimeout"  // Exclude specific test
   ./your_test "-*TestTimeout*"  // Exclude all tests with Function in the name
*/

class MyTest {
 public:
  static MyTest& GetInstance() {
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

  void RegisterTest(const std::string& test_name, std::function<void()> test) {
    sync_tests_.emplace_back(test_name, test);
  }

  void RegisterAsyncTest(const std::string& test_name,
                         std::function<std::future<void>()> test) {
    async_tests_.emplace_back(test_name, test);
  }

  void RegisterTestBeforeEach(const std::string& group_name,
                              std::function<void()> func) {
    test_before_each_[group_name] = func;
  }

  void RegisterTestAfterEach(const std::string& group_name,
                             std::function<void()> func) {
    test_after_each_[group_name] = func;
  }

  void RegisterTestBefore(const std::string& group_name,
                          std::function<void()> func) {
    test_before_[group_name] = func;
  }

  void RegisterTestAfter(const std::string& group_name,
                         std::function<void()> func) {
    test_after_[group_name] = func;
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
      } else if (arg == "-h") {
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
           sync_tests_.size() + async_tests_.size());

    std::unordered_map<std::string, bool> group_tested;

    // Run sync tests
    for (const auto& test_pair : sync_tests_) {
      const std::string& name = test_pair.first;
      const std::function<void()>& test = test_pair.second;
      if (!should_run(name)) {
        continue;
      }
      bool failure = false;
      bool skipped = false;

      auto group_name = name.substr(0, name.find(':'));
      if (!group_tested[group_name] && test_before_.count(group_name)) {
        test_before_[group_name]();
        group_tested[group_name] = true;
      }

      printf(
          "%s[ RUN      ]%s %s\n", colors[GREEN], colors[RESET], name.c_str());

      try {
        auto _ = OnScopeLeave::create([&silent]() {
          if (silent) {
            freopen("/dev/tty", "w", stdout);
            freopen("/dev/tty", "w", stderr);
          }
        });
        if (silent) {
          freopen("/dev/null", "w", stdout);
          freopen("/dev/null", "w", stderr);
        }
        if (test_before_each_.count(group_name)) {
          test_before_each_[group_name]();
        }
        test();
        if (test_after_each_.count(group_name)) {
          test_after_each_[group_name]();
        }
        ++num_success;
      } catch (const TestSkipException& e) {
        ++num_skipped;
        skipped = true;
        printf("\n%s\n", e.what());
      } catch (const TestTimeoutException& e) {
        ++num_failure;
        failure = true;
        printf("\n%s\n", e.what());
      } catch (const std::runtime_error& e) {
        ++num_failure;
        failure = true;
        printf("\n%s\n", e.what());
      } catch (const std::exception& e) {
        printf("\nException : %s\n", e.what());
        ++num_failure;
        failure = true;
      } catch (...) {
        printf("\nException : Unknown\n");
        ++num_failure;
        failure = true;
      }

      ++num_ran_tests;

      if (failure) {
        printf(
            "%s[  FAILED  ]%s %s\n", colors[RED], colors[RESET], name.c_str());
      } else if (skipped) {
        printf("%s[  SKIPPED ]%s %s\n",
               colors[YELLOW],
               colors[RESET],
               name.c_str());
      } else {
        printf("%s[       OK ]%s %s\n",
               colors[GREEN],
               colors[RESET],
               name.c_str());
      }
    }

    // Run async tests
    for (const auto& test_pair : async_tests_) {
      const std::string& name = test_pair.first;
      const std::function<std::future<void>()>& test = test_pair.second;
      if (!should_run(name)) {
        continue;
      }
      bool failure = false;
      bool skipped = false;

      auto group_name = name.substr(0, name.find(':'));
      if (!group_tested[group_name] && test_before_.count(group_name)) {
        test_before_[group_name]();
        group_tested[group_name] = true;
      }

      printf(
          "%s[ RUN      ]%s %s\n", colors[GREEN], colors[RESET], name.c_str());

      try {
        auto _ = OnScopeLeave::create([&silent]() {
          if (silent) {
            freopen("/dev/tty", "w", stdout);
            freopen("/dev/tty", "w", stderr);
          }
        });
        if (silent) {
          freopen("/dev/null", "w", stdout);
          freopen("/dev/null", "w", stderr);
        }

        if (test_before_each_.count(group_name)) {
          test_before_each_[group_name]();
        }
        auto future = test();
        future.get();
        if (test_after_each_.count(group_name)) {
          test_after_each_[group_name]();
        }
        ++num_success;
      } catch (const TestSkipException& e) {
        ++num_skipped;
        skipped = true;
        printf("\n%s\n", e.what());
      } catch (const TestTimeoutException& e) {
        ++num_failure;
        failure = true;
        printf("\n%s\n", e.what());
      } catch (const std::runtime_error& e) {
        ++num_failure;
        failure = true;
        printf("\n%s\n", e.what());
      } catch (const std::exception& e) {
        printf("\nException : %s\n", e.what());
        ++num_failure;
        failure = true;
      } catch (...) {
        printf("\nException : Unknown\n");
        ++num_failure;
        failure = true;
      }

      ++num_ran_tests;

      if (failure) {
        printf(
            "%s[  FAILED  ]%s %s\n", colors[RED], colors[RESET], name.c_str());
      } else if (skipped) {
        printf("%s[  SKIPPED ]%s %s\n",
               colors[YELLOW],
               colors[RESET],
               name.c_str());
      } else {
        printf("%s[       OK ]%s %s\n",
               colors[GREEN],
               colors[RESET],
               name.c_str());
      }
    }

    for (const auto& group : group_tested) {
      if (test_after_.count(group.first)) {
        test_after_[group.first]();
      }
    }

    printf("%s[==========]%s %d test case(s) ran.\n",
           colors[GREEN],
           colors[RESET],
           num_ran_tests);
    printf("%s[  PASSED  ]%s %d test(s).\n",
           colors[GREEN],
           colors[RESET],
           num_ran_tests - num_failure - num_skipped);
    if (0 != num_skipped) {
      printf("%s[  SKIPPED ]%s %d test(s):\n",
             colors[YELLOW],
             colors[RESET],
             num_skipped);
    }
    if (0 != num_failure) {
      printf("%s[  FAILED  ]%s %d test(s)\n",
             colors[RED],
             colors[RESET],
             num_failure);
    }
    return num_failure > 0 ? 1 : 0;
  }

  int default_timeout() { return default_timeout_; }

 private:
  static constexpr int kDefaultTimeoutMS = 10000;
  static constexpr const char* kCalVersion = "24.11.0";

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
    std::cout << "Usage: " << name << " [options]\n"
              << "Options:\n"
              << "  -p \"pattern\"  : Include tests matching the pattern\n"
              << "  -p \"-pattern\" : Exclude tests matching the pattern\n"
              << "  -t \"timeout\"  : Set the default timeout value (in "
                 "milliseconds, default: "
              << default_timeout << ")\n"
              << "  -c            : Disable color output\n"
              << "  -s            : Silent mode, suppress stdout and stderr\n"
              << "  -h            : Show this help message\n\n"
              << "Driven by MyTest (v" << kCalVersion << ")\n";
  }

  int default_timeout_;
  std::vector<std::pair<std::string, std::function<void()>>> sync_tests_;
  std::vector<std::pair<std::string, std::function<std::future<void>()>>>
      async_tests_;
  std::unordered_map<std::string, std::function<void()>> test_before_each_;
  std::unordered_map<std::string, std::function<void()>> test_after_each_;
  std::unordered_map<std::string, std::function<void()>> test_before_;
  std::unordered_map<std::string, std::function<void()>> test_after_;
  MyTest() : default_timeout_(kDefaultTimeoutMS) {}
  MyTest(const MyTest&) = delete;
  MyTest& operator=(const MyTest&) = delete;
};

#define TEST0(group, name)                                                     \
  void group##name();                                                          \
  struct group##name##_Register {                                              \
    group##name##_Register() {                                                 \
      MyTest::GetInstance().RegisterTest(#group ":" #name, group##name);       \
    }                                                                          \
  } group##name##_register;                                                    \
  void group##name()

#define TEST(group, name, ...)                                                 \
  void group##name##_impl();                                                   \
  std::future<void> group##name(int timeout_ms =                               \
                                    MyTest::GetInstance().default_timeout());  \
  struct group##name##_Register {                                              \
    group##name##_Register() {                                                 \
      MyTest::GetInstance().RegisterTest(                                      \
          #group ":" #name, []() { return group##name(__VA_ARGS__); });        \
    }                                                                          \
  } group##name##_register;                                                    \
  std::future<void> group##name(int timeout_ms) {                              \
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
  void group##name##_impl()

#define TEST_ASYNC(group, name, ...)                                           \
  void group##name##_impl(std::function<void()> done);                         \
  std::future<void> group##name(int timeout_ms =                               \
                                    MyTest::GetInstance().default_timeout());  \
  struct group##name##_Register {                                              \
    group##name##_Register() {                                                 \
      MyTest::GetInstance().RegisterAsyncTest(                                 \
          #group ":" #name, []() { return group##name(__VA_ARGS__); });        \
    }                                                                          \
  } group##name##_register;                                                    \
  std::future<void> group##name(int timeout_ms) {                              \
    auto promise = std::make_shared<std::promise<void>>();                     \
    auto future = promise->get_future();                                       \
    auto done = [promise]() { promise->set_value(); };                         \
    std::thread([done]() { group##name##_impl(done); }).detach();              \
    if (future.wait_for(std::chrono::milliseconds(timeout_ms)) ==              \
        std::future_status::timeout) {                                         \
      throw MyTest::TestTimeoutException(#group ":" #name);                    \
    }                                                                          \
    return future;                                                             \
  }                                                                            \
  void group##name##_impl(std::function<void()> done)

#define TEST_BEFORE_EACH(group)                                                \
  void group##_BeforeEach();                                                   \
  struct group##_BeforeEach_Register {                                         \
    group##_BeforeEach_Register() {                                            \
      MyTest::GetInstance().RegisterTestBeforeEach(#group,                     \
                                                   group##_BeforeEach);        \
    }                                                                          \
  } group##_BeforeEach_register;                                               \
  void group##_BeforeEach()

#define TEST_AFTER_EACH(group)                                                 \
  void group##_AfterEach();                                                    \
  struct group##_AfterEach_Register {                                          \
    group##_AfterEach_Register() {                                             \
      MyTest::GetInstance().RegisterTestAfterEach(#group, group##_AfterEach);  \
    }                                                                          \
  } group##_AfterEach_register;                                                \
  void group##_AfterEach()

#define TEST_BEFORE(group)                                                     \
  void group##_Before();                                                       \
  struct group##_Before_Register {                                             \
    group##_Before_Register() {                                                \
      MyTest::GetInstance().RegisterTestBefore(#group, group##_Before);        \
    }                                                                          \
  } group##_Before_register;                                                   \
  void group##_Before()

#define TEST_AFTER(group)                                                      \
  void group##_After();                                                        \
  struct group##_After_Register {                                              \
    group##_After_Register() {                                                 \
      MyTest::GetInstance().RegisterTestAfter(#group, group##_After);          \
    }                                                                          \
  } group##_After_register;                                                    \
  void group##_After()

#define TEST_SKIP(msg) throw MyTest::TestSkipException(msg)

#define RUN_ALL_TESTS(argc, argv) MyTest::GetInstance().RunAllTests(argc, argv)

#ifdef MYTEST_CONFIG_USE_MAIN
int main(int argc, char* argv[]) {
  return RUN_ALL_TESTS(argc, argv);
}
#endif

#ifndef __FILE_NAME__
#define __FILE_NAME__                                                          \
  std::string((strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__))
#endif

#define __LOC__ "(" + __FILE_NAME__ + ":" + std::to_string(__LINE__) + ")"

#define FORMAT_ERROR_MESSAGE(x, cond_str, y, msg)                              \
  std::stringstream ss;                                                        \
  ss << msg << std::string(" ") + __LOC__ << "\n";                             \
  ss << "  Expected : (" << #x << " " cond_str " " << #y << ")\n";             \
  ss << "    Actual : (" << x << " " cond_str " " << y << ")";

#define CHECK_CONDITION(cond, x, y, cond_str, msg, should_throw)               \
  if (cond) {                                                                  \
    FORMAT_ERROR_MESSAGE(x, y, cond_str, msg);                                 \
    if (should_throw) {                                                        \
      throw std::runtime_error(ss.str());                                      \
    } else {                                                                   \
      std::cout << "\n" << ss.str() << "\n";                                   \
    }                                                                          \
  }

#define EXPECT(cond, x, c, y, m) CHECK_CONDITION(!(cond), x, #c, y, m, false)
#define ASSERT(cond, x, c, y, m) CHECK_CONDITION(!(cond), x, #c, y, m, true)
#define EXPECT_EQ(x, y) EXPECT((x) == (y), x, ==, y, "EXPECT_EQ failed")
#define EXPECT_NE(x, y) EXPECT((x) != (y), x, !=, y, "EXPECT_NE failed")
#define ASSERT_EQ(x, y) ASSERT((x) == (y), x, ==, y, "ASSERT_EQ failed")
#define ASSERT_NE(x, y) ASSERT((x) != (y), x, !=, y, "ASSERT_NE failed")
