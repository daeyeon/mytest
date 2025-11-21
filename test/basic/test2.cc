#include <mytest.h>
#include "fixture.h"

extern Fixture f;
Fixture b;

TEST(TestSuite2, SyncTest) {
  b.count++;
}

TEST(TestSuite2, SyncTestFailure) {
  TEST_EXPECT_FAILURE();
  EXPECT_EQ(1, 0);
  b.expect++;
  ASSERT_EQ(1, 0);
  b.expect++;
  b.count++;
}

TEST_BEFORE_EACH(TestSuite2) {
  std::cout << "Before: each TestSuite2 test" << std::endl;
  b.before_each++;
}

TEST_AFTER_EACH(TestSuite2) {
  std::cout << "After : each TestSuite2 test" << std::endl;
  b.after_each++;
}

TEST_BEFORE(TestSuite2) {
  if (MyTest::Instance().IsJobIsolated()) { TEST_SKIP(); }
  std::cout << "\nRuns  : once before all TestSuite2 tests" << std::endl;
  b.before++;
}

TEST_AFTER(TestSuite2) {
  std::cout << "Runs  : once after all TestSuite2 tests\n" << std::endl;
  b.after++;

  if (MyTest::Instance().IsJobIsolated()) return;

  EXPECT_EQ(f.before, 1);
  EXPECT_EQ(f.after, 1);
  EXPECT_EQ(f.before_each, 8);
  EXPECT_EQ(f.after_each, 8);
  EXPECT_EQ(f.skip, 1);
  EXPECT_EQ(f.expect, 1);
  EXPECT_EQ(f.count, 4);

  EXPECT_EQ(b.before, 1);
  EXPECT_EQ(b.after, 1);
  EXPECT_EQ(b.before_each, 2);
  EXPECT_EQ(b.after_each, 2);
  EXPECT_EQ(b.skip, 0);
  EXPECT_EQ(b.expect, 1);
  EXPECT_EQ(b.count, 1);
}
