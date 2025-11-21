#include <mytest.h>
#include <future>
#include <thread>
#include "fixture.h"

auto& f_job = SharedFixture::Get("/test_suite1_job_fixture");

TEST(TestSuite1Job, SyncTest) {
  EXPECT_EQ(1, f_job->before);
  EXPECT_EQ(0, f_job->after);
  f_job->count++;
  ASSERT_NE(f_job->main_thread_id, std::this_thread::get_id());
}

TEST(TestSuite1Job, SyncTestFailure) {
  TEST_EXPECT_FAILURE();
  EXPECT_EQ(1, 0);
  f_job->expect++;
  ASSERT_EQ(1, 0);
  f_job->expect++;
  f_job->count++;
}

TEST(TestSuite1Job, SyncTestTimeout, 1000) {
  TEST_EXPECT_FAILURE();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  f_job->count++;
}

TEST0(TestSuite1Job, SyncTestOnCurrentThread) {
  f_job->count++;
  ASSERT_EQ(f_job->main_thread_id, std::this_thread::get_id());
}

TEST(TestSuite1Job, SyncTestSkip) {
  f_job->skip++;
  TEST_SKIP();
  f_job->count++;
}

TEST_ASYNC(TestSuite1Job, ASyncTest) {
  auto this_thread_id = std::this_thread::get_id();
  ASSERT_NE(f_job->main_thread_id, std::this_thread::get_id());
  std::async(std::launch::async, [&done, &this_thread_id]() {
    ASSERT_NE(this_thread_id, std::this_thread::get_id());
    std::this_thread::sleep_for(std::chrono::seconds(1));
    f_job->count++;
    done();
  }).get();
}

TEST_ASYNC(TestSuite1Job, ASyncTestTimeout, 1000) {
  TEST_EXPECT_FAILURE();
  std::async(std::launch::async, [&done]() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    f_job->count++;
    done();
  }).get();
}

TEST_ASYNC(TestSuite1Job, ASyncTestSkip) {
  TEST_SKIP();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  f_job->count++;
  done();
}

TEST_BEFORE_EACH(TestSuite1Job) {
  std::cout << "Before: each TestSuite1Job test" << std::endl;
  f_job->before_each++;
}

TEST_AFTER_EACH(TestSuite1Job) {
  std::cout << "After : each TestSuite1Job test" << std::endl;
  f_job->after_each++;
}

TEST_BEFORE(TestSuite1Job) {
  if (!MyTest::Instance().IsJobIsolated()) { TEST_SKIP(); }
  f_job.Create();
  std::cout << "\nRuns  : once before all TestSuite1Job tests" << std::endl;
  f_job->before++;
  f_job->main_thread_id = std::this_thread::get_id();
}

TEST_AFTER(TestSuite1Job) {
  std::cout << "Runs  : once after all TestSuite1Job tests\n" << std::endl;
  f_job->after++;

  EXPECT_EQ(f_job->before, 1);
  EXPECT_EQ(f_job->after, 1);
  EXPECT_EQ(f_job->before_each, 8);
  EXPECT_EQ(f_job->after_each, 8);
  EXPECT_EQ(f_job->skip, 1);
  EXPECT_EQ(f_job->expect, 1);
  EXPECT_EQ(f_job->count, 4);

  f_job.Remove();
}
