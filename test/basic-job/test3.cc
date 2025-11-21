#include <mytest.h>
#include "fixture.h"

auto& c_job = SharedFixture::Get("/test_suite3_job_fixture");
auto& d_job = SharedFixture::Get("/test_suite4_job_fixture");

TEST_EXCLUDE(TestSuite3Job)

TEST_EXCLUDE(TestSuite4Job, SyncTest2)

TEST(TestSuite3Job, SyncTest) {
  c_job->count++;
}

TEST(TestSuite4Job, SyncTest1) {
  d_job->count++;
}

TEST(TestSuite4Job, SyncTest2) {
  d_job->count++;
}

TEST_AFTER(TestSuite3Job) {
  std::cout << "\nRuns  : once before all TestSuite3Job tests" << std::endl;
  c_job->after++;
}

TEST_AFTER(TestSuite4Job) {
  std::cout << "Runs  : once after all TestSuite4Job tests\n" << std::endl;
  d_job->after++;

  EXPECT_EQ(c_job->after, 0);
  EXPECT_EQ(c_job->count, 0);

  EXPECT_EQ(d_job->after, 1);
  EXPECT_EQ(d_job->count, 1);

  c_job.Remove();
  d_job.Remove();
}
