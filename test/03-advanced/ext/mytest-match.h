/*
 * MyTest utility to match text with regular expressions.
 * Copyright 2026-present Daeyeon Jeong (daeyeon.dev@gmail.com)
 *
 * Licensed under the Apache License, Version 2.0.
 * See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#pragma once
#include <regex>
#include <sstream>
#include <string>
#include "mytest.h"

namespace mytest {

template <typename T>
std::string MatchToString(const T& value) {
  std::ostringstream out;
  out << value;
  return out.str();
}

inline void MatchInternal(const std::string& text, const std::string& pattern, bool should_match,
                          const std::string& loc, const char* macro_name, bool should_throw) {
  bool matched = false;
  std::string regex_error;

  try {
    matched = std::regex_search(text, std::regex(pattern));
  } catch (const std::regex_error& e) {
    regex_error = e.what();
  }

  const bool failed = !regex_error.empty() || (matched != should_match);
  if (!failed) return;

  std::ostringstream msg;
  msg << macro_name << " " << loc << "\n";
  if (!regex_error.empty()) {
    msg << "  Invalid regex : " << pattern << "\n";
    msg << "  Error         : " << regex_error;
  } else {
    msg << "  Expected : text " << (should_match ? "matches" : "does not match") << " pattern\n";
    msg << "    Text   : " << text << "\n";
    msg << "    Pattern: " << pattern;
  }

  MyTest::Instance().PrintCheckResult(msg.str());
  if (should_throw) throw MyTest::TestAssertException(msg.str());
  MyTest::Instance().MarkConditionPassed(false);
}

template <typename Text, typename Pattern>
void MatchInternal(const Text& text, const Pattern& pattern, bool should_match,
                   const std::string& loc, const char* macro_name, bool should_throw) {
  MatchInternal(MatchToString(text), MatchToString(pattern), should_match, loc, macro_name,
                should_throw);
}

}  // namespace mytest

#define EXPECT_MATCH(text, pattern)                                                                \
  mytest::MatchInternal((text), (pattern), true, _LOC, "EXPECT_MATCH failed", false)
#define ASSERT_MATCH(text, pattern)                                                                \
  mytest::MatchInternal((text), (pattern), true, _LOC, "ASSERT_MATCH failed", true)
#define EXPECT_NOT_MATCH(text, pattern)                                                            \
  mytest::MatchInternal((text), (pattern), false, _LOC, "EXPECT_NOT_MATCH failed", false)
#define ASSERT_NOT_MATCH(text, pattern)                                                            \
  mytest::MatchInternal((text), (pattern), false, _LOC, "ASSERT_NOT_MATCH failed", true)
