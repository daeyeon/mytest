#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>
#include <string>
#include "plugins/mytest-match.h"

TEST(Match, ExpectMatchSuccess) {
  EXPECT_MATCH("user-42", "user-[0-9]+");
}

TEST(Match, AssertMatchSuccess) {
  ASSERT_MATCH(std::string("status: ok"), "status: (ok|ready)");
}

TEST(Match, ExpectNotMatchSuccess) {
  EXPECT_NOT_MATCH("status: ok", "error-[0-9]+");
}

TEST(Match, AssertNotMatchSuccess) {
  ASSERT_NOT_MATCH("build: release-2026", "debug-[0-9]+");
}

TEST(Match, ExpectMatchFailure) {
  TEST_EXPECT_FAILURE();
  EXPECT_MATCH("status: ok", "error");
}

TEST(Match, AssertMatchFailure) {
  TEST_EXPECT_FAILURE();
  ASSERT_MATCH("status: ok", "error");
}

TEST(Match, ExpectNotMatchFailure) {
  TEST_EXPECT_FAILURE();
  EXPECT_NOT_MATCH("user-42", "user-[0-9]+");
}

TEST(Match, InvalidRegexFailure) {
  TEST_EXPECT_FAILURE();
  EXPECT_MATCH("abc", "[");
}
