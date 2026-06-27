#include <mytest.h>
#include "../test-helpers.h"

namespace {
struct NoStreamValue {
  int value;
};

bool operator==(const NoStreamValue& left, const NoStreamValue& right) {
  return left.value == right.value;
}
}  // namespace

static bool IsChildRun() { return getenv("IS_SPAWNED_CHILD") != nullptr; }

TEST(AssertEval, FailedExpectOnce) {
  if (IsChildRun()) {
    TEST_SKIP("In child run");
  }

  RunResult result = ExecuteSelfResult({"-p", "^AssertEvalCase:Fail$", "-c", nullptr});
  EXPECT_EQ(result.exit_status, 1);
  EXPECT_NE(result.output.find("ASSERTION_EVALUATED_ONCE"), std::string::npos);
}

TEST(AssertEvalCase, Fail) {
  if (!IsChildRun()) return;

  int left = 0;
  int right = 0;

  // This fails, but each side must still be evaluated only once.
  EXPECT_EQ(++left, ++right + 1);

  // Failure formatting must reuse the captured values, not re-run ++left/++right.
  if (left == 1 && right == 1) {
    printf("ASSERTION_EVALUATED_ONCE\n");
  }
}

TEST(AssertPrint, UnprintableFallback) {
  if (IsChildRun()) {
    TEST_SKIP("In child run");
  }

  RunResult result = ExecuteSelfResult({"-p", "^AssertPrintCase:Fail$", "-c", nullptr});
  EXPECT_EQ(result.exit_status, 1);
  EXPECT_NE(result.output.find("<unprintable>"), std::string::npos);
}

TEST(AssertPrintCase, Fail) {
  if (!IsChildRun()) return;

  EXPECT_EQ(NoStreamValue{1}, NoStreamValue{2});
}
