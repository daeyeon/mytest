#pragma once

#include "mytest.h"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mytest {

namespace {

inline std::string ApplyTemplate(
    std::string_view tmpl,
    const std::unordered_map<std::string, std::string>& replacements) {
  std::string result(tmpl);
  for (const auto& [key, value] : replacements) {
    std::string placeholder = std::string("{{") + key + "}}";
    size_t pos = 0;
    while ((pos = result.find(placeholder, pos)) != std::string::npos) {
      result.replace(pos, placeholder.size(), value);
      pos += value.size();
    }
  }
  return result;
}

inline std::string EscapeXml(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '\"':
        out += "&quot;";
        break;
      case '\'':
        out += "&apos;";
        break;
      case '\n':
        out += "&#10;";
        break;
      case '\r':
        out += "&#13;";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

inline std::string UnescapeXml(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size();) {
    if (value[i] != '&') {
      out.push_back(value[i++]);
      continue;
    }

    if (value.compare(i, 5, "&amp;") == 0) {
      out.push_back('&');
      i += 5;
    } else if (value.compare(i, 4, "&lt;") == 0) {
      out.push_back('<');
      i += 4;
    } else if (value.compare(i, 4, "&gt;") == 0) {
      out.push_back('>');
      i += 4;
    } else if (value.compare(i, 6, "&quot;") == 0) {
      out.push_back('"');
      i += 6;
    } else if (value.compare(i, 6, "&apos;") == 0) {
      out.push_back('\'');
      i += 6;
    } else if (value.compare(i, 5, "&#10;") == 0) {
      out.push_back('\n');
      i += 5;
    } else if (value.compare(i, 5, "&#13;") == 0) {
      out.push_back('\r');
      i += 5;
    } else {
      out.push_back(value[i++]);
    }
  }
  return out;
}

inline std::vector<TestResult> LoadExistingGTestResults(
    const std::string& path) {
  std::ifstream input(path);
  if (!input.is_open()) return {};

  const std::regex testsuite_re(R"regex(<testsuite[^>]*name="([^"]+)")regex");
  const std::regex testcase_re(
      R"regex(<testcase[^>]*name="([^"]+)"[^>]*status="([^"]+)")regex");
  const std::regex failure_re(R"regex(<failure[^>]*message="([^"]*)")regex");
  const std::regex skipped_re(R"regex(<skipped[^>]*message="([^"]*)")regex");
  const std::regex system_out_re(R"regex(<system-out>(.*)</system-out>)regex");

  std::vector<TestResult> results;
  std::string current_suite;
  std::optional<TestResult> current_case;
  std::string line;
  while (std::getline(input, line)) {
    std::smatch match;
    if (std::regex_search(line, match, testsuite_re)) {
      current_suite = UnescapeXml(match[1].str());
      continue;
    }

    if (std::regex_search(line, match, testcase_re)) {
      TestResult result;
      result.suite = current_suite;
      result.name = UnescapeXml(match[1].str());
      const std::string status = match[2].str();
      result.skipped = status == "notrun";
      result.failure = false;
      result.message.clear();
      const bool self_closing = line.find("/>") != std::string::npos;
      if (self_closing) {
        results.push_back(std::move(result));
        current_case.reset();
      } else {
        current_case = std::move(result);
      }
      continue;
    }

    if (!current_case.has_value()) continue;

    if (std::regex_search(line, match, failure_re)) {
      current_case->failure = true;
      current_case->skipped = false;
      current_case->message = UnescapeXml(match[1].str());
    } else if (std::regex_search(line, match, skipped_re)) {
      current_case->skipped = true;
      current_case->message = UnescapeXml(match[1].str());
    } else if (std::regex_search(line, match, system_out_re)) {
      current_case->message = UnescapeXml(match[1].str());
    }

    if (line.find("</testcase>") != std::string::npos) {
      results.push_back(std::move(*current_case));
      current_case.reset();
    }
  }
  return results;
}

inline std::string CurrentTimestamp() {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
  return oss.str();
}

}  // namespace

class GTestXmlReporter : public Reporter {
 public:
  void OnComplete(const std::vector<TestResult>& results,
                  const Summary&,
                  const Options& options) override {
    const std::string path =
        options.output_path.empty() ? "test_report.xml" : options.output_path;
    WriteGTestXml(path, results);
  }

 private:
  static void WriteGTestXml(const std::string& path,
                            const std::vector<TestResult>& new_results) {
    static constexpr std::string_view kTestsuitesTemplate =
        R"(<?xml version="1.0" encoding="UTF-8"?>
<testsuites tests="{{tests}}" failures="{{failures}}" disabled="0" errors="0" time="0" timestamp="{{timestamp}}">
{{body}}</testsuites>
)";
    static constexpr std::string_view kTestsuiteTemplate =
        R"(  <testsuite name="{{name}}" tests="{{tests}}" failures="{{failures}}" disabled="0" errors="0" skipped="{{skipped}}" time="0">
{{testcases}}  </testsuite>
)";
    static constexpr std::string_view kTestcaseTemplate =
        R"(    <testcase name="{{name}}" status="{{status}}" time="0" classname="{{classname}}">
{{body}}    </testcase>
)";
    static constexpr std::string_view kTestcaseSelfClosingTemplate =
        R"(    <testcase name="{{name}}" status="{{status}}" time="0" classname="{{classname}}"/>
)";
    static constexpr std::string_view kFailureTemplate =
        R"(      <failure message="{{message}}" type=""/>
)";
    static constexpr std::string_view kSkippedTemplate =
        R"(      <skipped message="{{message}}"/>
)";
    static constexpr std::string_view kSystemOutTemplate =
        R"(      <system-out>{{message}}</system-out>
)";

    auto all_results = LoadExistingGTestResults(path);
    all_results.reserve(all_results.size() + new_results.size());
    all_results.insert(
        all_results.end(), new_results.begin(), new_results.end());

    const int aggregated_total = static_cast<int>(all_results.size());
    int aggregated_failures = 0;
    int aggregated_skipped = 0;
    for (const auto& res : all_results) {
      aggregated_failures += res.failure ? 1 : 0;
      aggregated_skipped += res.skipped ? 1 : 0;
    }

    std::map<std::string, std::vector<TestResult>> suites;
    for (const auto& res : all_results) {
      suites[res.suite].push_back(res);
    }

    std::ostringstream suites_stream;
    for (const auto& [suite_name, cases] : suites) {
      std::ostringstream cases_stream;
      int suite_failures = 0;
      int suite_skipped = 0;
      for (const auto& res : cases) {
        suite_failures += res.failure ? 1 : 0;
        suite_skipped += res.skipped ? 1 : 0;

        const std::string status = res.skipped ? "notrun" : "run";
        std::string body;
        if (res.failure) {
          body = ApplyTemplate(kFailureTemplate,
                               {{"message", EscapeXml(res.message)}});
        } else if (res.skipped) {
          body = ApplyTemplate(kSkippedTemplate,
                               {{"message", EscapeXml(res.message)}});
        } else if (!res.message.empty()) {
          body = ApplyTemplate(kSystemOutTemplate,
                               {{"message", EscapeXml(res.message)}});
        }

        const std::unordered_map<std::string, std::string> testcase_common = {
            {"name", EscapeXml(res.name)},
            {"status", status},
            {"classname", EscapeXml(suite_name)}};

        if (body.empty()) {
          cases_stream << ApplyTemplate(kTestcaseSelfClosingTemplate,
                                        testcase_common);
        } else {
          auto map_with_body = testcase_common;
          map_with_body.insert({"body", body});
          cases_stream << ApplyTemplate(kTestcaseTemplate, map_with_body);
        }
      }

      suites_stream << ApplyTemplate(
          kTestsuiteTemplate,
          {{"name", EscapeXml(suite_name)},
           {"tests", std::to_string(cases.size())},
           {"failures", std::to_string(suite_failures)},
           {"skipped", std::to_string(suite_skipped)},
           {"testcases", cases_stream.str()}});
    }

    std::ofstream xml(path);
    if (!xml.is_open()) {
      std::cerr << "Failed to write gtest XML report: " << path << std::endl;
      return;
    }
    xml << ApplyTemplate(kTestsuitesTemplate,
                         {{"tests", std::to_string(aggregated_total)},
                          {"failures", std::to_string(aggregated_failures)},
                          {"timestamp", CurrentTimestamp()},
                          {"body", suites_stream.str()}});
  }
};

}  // namespace mytest
