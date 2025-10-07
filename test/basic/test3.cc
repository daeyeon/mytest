#include <mytest.h>
#include "fixture.h"

Fixture c, d;

TEST_EXCLUDE(TestSuite3)

TEST_EXCLUDE(TestSuite4, SyncTest2)

TEST(TestSuite3, SyncTest) {
  c.count++;
}

TEST(TestSuite4, SyncTest1) {
  d.count++;
}

TEST(TestSuite4, SyncTest2) {
  d.count++;
}

TEST_AFTER(TestSuite3) {
  std::cout << "\nRuns  : once before all TestSuite3 tests" << std::endl;
  c.after++;
}

TEST_AFTER(TestSuite4) {
  std::cout << "Runs  : once after all TestSuite4 tests\n" << std::endl;
  d.after++;

  EXPECT_EQ(c.after, 0);
  EXPECT_EQ(c.count, 0);

  EXPECT_EQ(d.after, 1);
  EXPECT_EQ(d.count, 1);
}
