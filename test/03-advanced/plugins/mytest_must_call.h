/*
 * MyTest utility to verify function calls.
 * Copyright 2026-present Daeyeon Jeong (daeyeon.dev@gmail.com)
 *
 * Licensed under the Apache License, Version 2.0.
 * See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include "mytest.h"

namespace mytest {

class CallTracker {
 public:
  CallTracker(int expected, std::string loc) : expected_(expected), actual_(0), loc_(loc) {}
  void Hit() { actual_++; }
  bool Validate(std::string& out_msg) const {
    if (actual_ == expected_) return true;
    out_msg = "Function expected to be called " + std::to_string(expected_) +
              " times, but called " + std::to_string(static_cast<int>(actual_)) + " times. " + loc_;
    return false;
  }

 private:
  int expected_;
  std::atomic<int> actual_;
  std::string loc_;
};

template <typename F>
auto MustCallInternal(F&& f, int expected, std::string loc) {
  auto tracker = std::make_shared<CallTracker>(expected, loc);
  MyTest::Instance().RegisterPostTestTask([tracker]() {
    std::string msg;
    if (!tracker->Validate(msg)) {
      MyTest::Instance().PrintCheckResult(msg);
      MyTest::Instance().MarkConditionPassed(false);
    }
  });
  return [f = std::forward<F>(f), tracker](auto&&... args) mutable {
    tracker->Hit();
    return f(std::forward<decltype(args)>(args)...);
  };
}

template <typename F>
auto MustCallInternal(F&& f, std::string loc) {
  return MustCallInternal(std::forward<F>(f), 1, loc);
}

}  // namespace mytest

#define MUST_CALL(f, ...) mytest::MustCallInternal(f, ##__VA_ARGS__, _LOC)
#define MUST_NOT_CALL(f) mytest::MustCallInternal(f, 0, _LOC)
