// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mbo/log/log_timing.h"

#include <source_location>
#include <string>
#include <string_view>

#include "absl/base/log_severity.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/log/initialize.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// NOLINTBEGIN(abseil-no-namespace,cppcoreguidelines-avoid-non-const-global-variables)

ABSL_DECLARE_FLAG(absl::Duration, mbo_log_timing_min_duration);

ABSL_DECLARE_FLAG(absl::LogSeverity, mbo_log_timing_min_severity_always);

// NOLINTEND(abseil-no-namespace,cppcoreguidelines-avoid-non-const-global-variables)

namespace mbo::log::log_internal {

using ::testing::AnyOf;
using ::testing::ContainsRegex;
using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::Sequence;

struct StripFunctionNameTest : ::testing::Test {
  static std::string StripFunctionName(std::string_view str) { return LogTimingImpl::StripFunctionName(str); }

  static std::string_view ReverseFindSpaceSkipPastMatchingBrackets(std::string_view str) {
    return LogTimingImpl::ReverseFindSpaceSkipPastMatchingBrackets(str);
  }

  static std::string_view ReverseStripAngleBrackets(std::string_view str) {
    return LogTimingImpl::ReverseStripAngleBrackets(str);
  }
};

namespace {

TEST_F(StripFunctionNameTest, Empty) {
  EXPECT_THAT(ReverseFindSpaceSkipPastMatchingBrackets(""), "");
  EXPECT_THAT(ReverseStripAngleBrackets(""), "");
  EXPECT_THAT(StripFunctionName(""), "");
}

TEST_F(StripFunctionNameTest, Simple) {
  EXPECT_THAT(StripFunctionName("already stripped"), "already stripped");
  EXPECT_THAT(StripFunctionName("auto foo()"), "foo");
  EXPECT_THAT(StripFunctionName("auto foo(int, int)"), "foo");
}

TEST_F(StripFunctionNameTest, StdFunction) {
  EXPECT_THAT(StripFunctionName("std::function<void(void)> Foo(bool x, std::function<void(bool)>) const &"), "Foo");
}

TEST_F(StripFunctionNameTest, TemplateFunction) {
  EXPECT_THAT(
      StripFunctionName(
          "std::function<void(void)> Foo<int, std::function<void(int, bool)>>(bool x, "
          "std::function<void(bool)>) const &"),
      "Foo");
  EXPECT_THAT(
      StripFunctionName("std::function<void(void)> Foo<int, bool>(bool x, std::function<void(bool)>) const &"), "Foo");
  EXPECT_THAT(StripFunctionName("auto Foo<std::pair<int, int>>::Bar()"), "Foo<std::pair<int, int>>::Bar");
}

TEST_F(StripFunctionNameTest, UnmatchedTemplateSuffixIsPreserved) {
  EXPECT_THAT(StripFunctionName("auto Foo>()"), "auto Foo>");
}

TEST_F(StripFunctionNameTest, Operator) {
  EXPECT_THAT(StripFunctionName("auto Foo::operator()"), "Foo::operator()");
  EXPECT_THAT(StripFunctionName("auto Foo::operator()()"), "Foo::operator()");
  EXPECT_THAT(StripFunctionName("auto Foo::()"), "Foo::()");
  EXPECT_THAT(
      StripFunctionName("std::function<void(bool, int)> Foo::operator()(std::function<void(bool, int))"),
      "Foo::operator()");
}

TEST_F(StripFunctionNameTest, LambdaInMethod) {
  EXPECT_THAT(
      StripFunctionName("auto ns::Class::Method(X &, const Y &)::(anonymous class)::operator()(X x, Y y)"),
      "ns::Class::Method(X &, const Y &)::[]()")
      << "\nExpected: Should only strip the parameters, even if empty like here `()`.";
}

TEST_F(StripFunctionNameTest, LambdaInLambdaInMethod) {
  EXPECT_THAT(
      StripFunctionName(
          R"(auto ns::Class::Method(X &, const Y &)::(anonymous class)::operator()()::(anonymous class)::operator()() const)"),
      "ns::Class::Method(X &, const Y &)::[]()::[]()")
      << "\nExpected: Should only strip the parameters, even in a const lambda with empty parameter list: `() const`.";
}

TEST_F(StripFunctionNameTest, ThisTest) {
  EXPECT_THAT(
      StripFunctionName("mbo::log::log_internal::(anonymous namespace)::LogTimingTest_LogFormat_Test::TestBody"),
      "mbo::log::log_internal::(anonymous namespace)::LogTimingTest_LogFormat_Test::TestBody");
}

struct LogTimingTest : StripFunctionNameTest {
  static void SetUpTestSuite() { absl::InitializeLog(); }

  LogTimingTest() : log(absl::MockLogDefault::kDisallowUnexpected) { log.StartCapturingLogs(); }

  template<typename MessageMatcher>
  auto&& ExpectLog(absl::LogSeverity severity, MessageMatcher&& message_matcher) & {
    return EXPECT_CALL(
        log, Log(severity, AnyOf(EndsWith(__FILE__), EndsWith("/mbo/log/log_timing.h")),
                 std::forward<MessageMatcher>(message_matcher)));
  }

  template<typename MessageMatcher>
  auto&& ExpectLogConst(absl::LogSeverity severity, const MessageMatcher& message_matcher) & {
    return EXPECT_CALL(
        log, Log(severity, AnyOf(EndsWith(__FILE__), EndsWith("/mbo/log/log_timing.h")), message_matcher));
  }

  absl::ScopedMockLog log;
};

TEST_F(LogTimingTest, LogFormat) {
  absl::SetFlag(&FLAGS_mbo_log_timing_min_duration, absl::ZeroDuration());
  absl::SetFlag(&FLAGS_mbo_log_timing_min_severity_always, absl::LogSeverity::kError);
  const std::string function = absl::StrReplaceAll(
      // We read the function we will be logging from `std::source_location`.
      StripFunctionName(std::source_location::current().function_name()),
      // That function may contain brackets that need to be escaped.
      {
          {"(", "\\("},
          {")", "\\)"},
      });
  const Sequence sequence;
  const std::string expected_log1 = absl::StrFormat(".*LogTiming\\(([0-9:.]+[mnu]s|0) @ (%s)*\\)$", function);
  const std::string expected_log2 = absl::StrFormat(".*LogTiming\\(([0-9:.]+[mnu]s|0) @ (%s)*\\): Foo$", function);
  ExpectLogConst(absl::LogSeverity::kInfo, ContainsRegex(expected_log1)).Times(1).InSequence(sequence);
  ExpectLogConst(absl::LogSeverity::kInfo, ContainsRegex(expected_log2)).Times(1).InSequence(sequence);
  (void)LogTiming();  // Manually discarding the result means, this one logs immediately.
  auto done2 = LogTiming() << "Foo";
}

TEST_F(LogTimingTest, LogSequence) {
  absl::SetFlag(&FLAGS_mbo_log_timing_min_duration, absl::ZeroDuration());
  absl::SetFlag(&FLAGS_mbo_log_timing_min_severity_always, absl::LogSeverity::kError);
  const Sequence sequence;
  // Logging occurs in reverse order driven by the reverse order un-scoping.
  ExpectLog(absl::LogSeverity::kInfo, HasSubstr("Bar")).Times(1).InSequence(sequence);
  ExpectLog(absl::LogSeverity::kInfo, HasSubstr("Foo")).Times(1).InSequence(sequence);
  auto done1 = LogTiming() << "Foo";
  auto done2 = LogTiming() << "Bar";
}

TEST_F(LogTimingTest, TooShort) {
  absl::SetFlag(&FLAGS_mbo_log_timing_min_duration, absl::InfiniteDuration());
  absl::SetFlag(&FLAGS_mbo_log_timing_min_severity_always, absl::LogSeverity::kError);
  ExpectLog(absl::LogSeverity::kInfo, HasSubstr("Foo")).Times(0);
  auto done = LogTiming() << "Foo";
}

TEST_F(LogTimingTest, Always) {
  absl::SetFlag(&FLAGS_mbo_log_timing_min_duration, absl::InfiniteDuration());
  absl::SetFlag(&FLAGS_mbo_log_timing_min_severity_always, absl::LogSeverity::kInfo);
  ExpectLog(absl::LogSeverity::kInfo, HasSubstr("Foo")).Times(1);
  auto done = LogTiming() << "Foo";
}

TEST_F(LogTimingTest, AlwaysManualSeverity) {
  absl::SetFlag(&FLAGS_mbo_log_timing_min_duration, absl::InfiniteDuration());
  absl::SetFlag(&FLAGS_mbo_log_timing_min_severity_always, absl::LogSeverity::kError);
  ExpectLog(absl::LogSeverity::kError, HasSubstr("Foo")).Times(1);
  auto done = LogTiming({.severity = absl::LogSeverity::kError}) << "Foo";
}

TEST_F(LogTimingTest, ManualSeverity) {
  absl::SetFlag(&FLAGS_mbo_log_timing_min_duration, absl::ZeroDuration());
  absl::SetFlag(&FLAGS_mbo_log_timing_min_severity_always, absl::LogSeverity::kError);
  ExpectLog(absl::LogSeverity::kWarning, HasSubstr("Foo")).Times(1);
  auto done = LogTiming({.severity = absl::LogSeverity::kWarning}) << "Foo";
}

}  // namespace
}  // namespace mbo::log::log_internal
