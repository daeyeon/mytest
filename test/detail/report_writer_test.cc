#include <mytest.h>
#include <mytest_report.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* kReportPath = "report_writer_test.xml";

std::string ReadFile(const char* path) {
  std::ifstream in(path);
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

mytest::TestResult MakeResult(const std::string& suite,
                              const std::string& name,
                              bool failure = false,
                              bool skipped = false,
                              std::string message = {}) {
  mytest::TestResult res;
  res.suite = suite;
  res.name = name;
  res.failure = failure;
  res.skipped = skipped;
  res.message = std::move(message);
  return res;
}

}  // namespace

TEST(Report, WritesXmlOnFirstRun) {
  std::remove(kReportPath);

  mytest::GTestXmlReporter reporter;
  mytest::Summary summary{};
  mytest::Options options;
  options.output_path = kReportPath;

  std::vector<mytest::TestResult> results{
      MakeResult("ReportSuite", "Passes", false, false, "all good"),
      MakeResult("ReportSuite", "Fails", true, false, "something broke"),
  };

  reporter.OnComplete(results, summary, options);

  ASSERT(std::ifstream(kReportPath).good());
  const std::string content = ReadFile(kReportPath);

  EXPECT(content.find("<testsuites tests=\"2\" failures=\"1\"") !=
         std::string::npos);
  EXPECT(content.find("ReportSuite") != std::string::npos);
  EXPECT(content.find("name=\"Fails\"") != std::string::npos);
  EXPECT(content.find("message=\"something broke\"") != std::string::npos);
}

TEST(Report, AppendsExistingResults) {
  mytest::GTestXmlReporter reporter;
  mytest::Summary summary{};
  mytest::Options options;
  options.output_path = kReportPath;

  std::vector<mytest::TestResult> results{
      MakeResult("AnotherSuite", "Skipped", false, true, "not run"),
  };
  reporter.OnComplete(results, summary, options);

  const std::string content = ReadFile(kReportPath);
  EXPECT(content.find("<testsuites tests=\"3\" failures=\"1\"") !=
         std::string::npos);
  EXPECT(content.find("AnotherSuite") != std::string::npos);
  EXPECT(content.find("name=\"Skipped\"") != std::string::npos);
  EXPECT(content.find("message=\"not run\"") != std::string::npos);
}

TEST_AFTER(Report) {
  std::remove(kReportPath);
}
