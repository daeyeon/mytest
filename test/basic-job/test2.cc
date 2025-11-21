#include <mytest.h>
#include "fixture.h"

extern SharedFixture& f_job;
auto& b_job = SharedFixture::Get("/test_suite2_job_fixture");

TEST(TestSuite2Job, SyncTest) {
  b_job->count++;
}

TEST(TestSuite2Job, SyncTestFailure) {
  TEST_EXPECT_FAILURE();
  EXPECT_EQ(1, 0);
  b_job->expect++;
  ASSERT_EQ(1, 0);
  b_job->expect++;
  b_job->count++;
}

TEST_BEFORE_EACH(TestSuite2Job) {
  std::cout << "Before: each TestSuite2Job test" << std::endl;
  b_job->before_each++;
}

TEST_AFTER_EACH(TestSuite2Job) {
  std::cout << "After : each TestSuite2Job test" << std::endl;
  b_job->after_each++;
}

TEST_BEFORE(TestSuite2Job) {
  if (!MyTest::Instance().IsJobIsolated()) { TEST_SKIP(); }
  b_job.Create();
  std::cout << "\nRuns  : once before all TestSuite2Job tests" << std::endl;
  b_job->before++;
}

TEST_AFTER(TestSuite2Job) {
  std::cout << "Runs  : once after all TestSuite2Job tests\n" << std::endl;
  b_job->after++;

  EXPECT_EQ(f_job->before, 1);
  EXPECT_EQ(f_job->after, 1);
  EXPECT_EQ(f_job->before_each, 8);
  EXPECT_EQ(f_job->after_each, 8);
  EXPECT_EQ(f_job->skip, 1);
  EXPECT_EQ(f_job->expect, 1);
  EXPECT_EQ(f_job->count, 4);

  EXPECT_EQ(b_job->before, 1);
  EXPECT_EQ(b_job->after, 1);
  EXPECT_EQ(b_job->before_each, 2);
  EXPECT_EQ(b_job->after_each, 2);
  EXPECT_EQ(b_job->skip, 0);
  EXPECT_EQ(b_job->expect, 1);
  EXPECT_EQ(b_job->count, 1);

  b_job.Remove();
}
