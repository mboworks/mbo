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

#include "mbo/testing/status.h"

#include <sstream>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::testing {
namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
// NOLINTBEGIN(readability-magic-numbers)

using ::mbo::testing::IsOk;          // Explicit
using ::mbo::testing::IsOkAndHolds;  // Explicit
using ::mbo::testing::StatusIs;      // Explicit
using ::mbo::testing::testing_internal::IsOkAndHoldsMatcher;
using ::mbo::testing::testing_internal::IsOkMatcher;
using ::mbo::testing::testing_internal::StatusIsMatcher;
using ::testing::_;
using ::testing::AnyOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Key;
using ::testing::Ne;
using ::testing::Not;
using ::testing::Pair;
using ::testing::SizeIs;

class StatusMatcherTest : public ::testing::Test {
 public:
  template<typename Matcher>
  static std::string Describe(const Matcher& matcher) {
    std::ostringstream os;
    matcher.DescribeTo(&os);
    return os.str();
  }

  template<typename Matcher>
  static std::string DescribeNegation(const Matcher& matcher) {
    std::ostringstream os;
    matcher.DescribeNegationTo(&os);
    return os.str();
  }

  template<typename Matcher, typename V>
  static std::pair<bool, std::string> MatchAndExplain(const Matcher& matcher, const V& value) {
    ::testing::StringMatchResultListener os;
    const bool result = matcher.MatchAndExplain(value, &os);
    return {result, os.str()};
  }
};

TEST_F(StatusMatcherTest, StatusCodeAcceptsAndReturnsIntegralCodes) {
  const testing_internal::StatusCode code(7);
  EXPECT_THAT(static_cast<int>(code), Eq(7));
}

TEST_F(StatusMatcherTest, IsOk) {
  EXPECT_THAT(absl::OkStatus(), IsOk());
  EXPECT_THAT(absl::AbortedError(""), Not(IsOk()));
  absl::StatusOr<int> val;
  EXPECT_THAT(val, Not(IsOk()));
  val = 42;
  EXPECT_THAT(val, IsOk());
  val = absl::UnknownError("");
  EXPECT_THAT(val, Not(IsOk()));

  {
    const ::testing::Matcher<absl::Status> matcher = IsOkMatcher();
    EXPECT_THAT(Describe(matcher), "is OK");
    EXPECT_THAT(DescribeNegation(matcher), "is not OK");
    EXPECT_THAT(MatchAndExplain(matcher, absl::OkStatus()), Pair(true, ""));
    EXPECT_THAT(
        MatchAndExplain(matcher, absl::UnknownError("Message")), Pair(false, "which has status UNKNOWN: 'Message'"));
  }
  {
    const ::testing::Matcher<absl::StatusOr<int>> matcher = IsOkMatcher();
    EXPECT_THAT(Describe(matcher), "is OK");
    EXPECT_THAT(DescribeNegation(matcher), "is not OK");
    EXPECT_THAT(MatchAndExplain(matcher, 42), Pair(true, ""));
    EXPECT_THAT(
        MatchAndExplain(matcher, absl::UnknownError("Message")), Pair(false, "which has status UNKNOWN: 'Message'"));
  }
}

TEST_F(StatusMatcherTest, StatusIs) {
  EXPECT_THAT(absl::OkStatus(), StatusIs(absl::StatusCode::kOk));
  EXPECT_THAT(absl::OkStatus(), StatusIs(absl::StatusCode::kOk, ""));
  EXPECT_THAT(absl::AbortedError(""), StatusIs(absl::StatusCode::kAborted));
  EXPECT_THAT(absl::AbortedError("a-sub-c"), StatusIs(absl::StatusCode::kAborted, "a-sub-c"));
  EXPECT_THAT(absl::AbortedError("a-sub-c"), StatusIs(absl::StatusCode::kAborted, HasSubstr("-sub-")));
  EXPECT_THAT(absl::AbortedError("a-sub-c"), StatusIs(absl::StatusCode::kAborted, Not(HasSubstr("not-a-substring"))));
  absl::StatusOr<int> test;
  EXPECT_THAT(test, Not(StatusIs(absl::StatusCode::kOk)));
  test = 42;
  EXPECT_THAT(test, StatusIs(absl::StatusCode::kOk));
  test = absl::UnknownError("");
  EXPECT_THAT(test, StatusIs(absl::StatusCode::kUnknown));

  {
    const StatusIsMatcher matcher(absl::StatusCode::kOk, "x");
    EXPECT_THAT(
        Describe(matcher), "has a status code that is equal to OK, and has an error message that is equal to \"x\"");
    EXPECT_THAT(
        DescribeNegation(matcher),
        "not (has a status code that is equal to OK, and has an error message that is equal to \"x\")");
    EXPECT_THAT(MatchAndExplain(matcher, absl::OkStatus()), Pair(true, "")) << "Must ignore status message if Ok";
  }
  {
    const StatusIsMatcher matcher(absl::StatusCode::kOk, "");
    EXPECT_THAT(
        Describe(matcher), "has a status code that is equal to OK, and has an error message that is equal to \"\"");
    EXPECT_THAT(
        DescribeNegation(matcher),
        "not (has a status code that is equal to OK, and has an error message that is equal to \"\")");
    EXPECT_THAT(
        MatchAndExplain(matcher, absl::UnknownError("")),
        Pair(false, "which has status code UNKNOWN and a matching message"));
    EXPECT_THAT(
        MatchAndExplain(matcher, absl::UnknownError("Message")),
        Pair(false, "which has status code UNKNOWN and the message 'Message'"));
    EXPECT_THAT(
        MatchAndExplain(StatusIsMatcher(absl::StatusCode::kUnknown, "Wanted"), absl::UnknownError("")),
        Pair(false, "which has status code UNKNOWN and an empty message"));
  }
  {
    const StatusIsMatcher matcher(absl::StatusCode::kCancelled, "Wanted");
    EXPECT_THAT(
        Describe(matcher),
        "has a status code that is equal to CANCELLED, and has an error message that is equal to \"Wanted\"");
    EXPECT_THAT(
        DescribeNegation(matcher),
        "not (has a status code that is equal to CANCELLED, and has an error message that is equal to \"Wanted\")");
    EXPECT_THAT(MatchAndExplain(matcher, absl::OkStatus()), Pair(false, "which has status code OK"));
    EXPECT_THAT(
        MatchAndExplain(matcher, absl::AbortedError("Wanted")),
        Pair(false, "which has status code ABORTED and a matching message"));
    EXPECT_THAT(
        MatchAndExplain(matcher, absl::CancelledError("Message")),
        Pair(false, "which has status code CANCELLED and the message 'Message'"));
  }
  {
    const StatusIsMatcher matcher(absl::StatusCode::kDataLoss, IsEmpty());
    EXPECT_THAT(
        Describe(matcher), "has a status code that is equal to DATA_LOSS, and has an error message that is empty");
    EXPECT_THAT(
        DescribeNegation(matcher),
        "not (has a status code that is equal to DATA_LOSS, and has an error message that is empty)");
    EXPECT_THAT(
        MatchAndExplain(matcher, absl::DataLossError("Message")),
        Pair(false, "which has status code DATA_LOSS and the message 'Message' (whose size is 7)"));
  }
}

TEST_F(StatusMatcherTest, StatusIsWithCodeMatcher) {
  // The status code argument may be a gMock matcher, not just a literal code.
  EXPECT_THAT(absl::NotFoundError("nope"), StatusIs(absl::StatusCode::kNotFound));  // literal still works
  EXPECT_THAT(
      absl::NotFoundError("nope"), StatusIs(AnyOf(absl::StatusCode::kNotFound, absl::StatusCode::kUnavailable)));
  EXPECT_THAT(absl::AbortedError("x"), StatusIs(Ne(absl::StatusCode::kOk)));
  EXPECT_THAT(absl::OkStatus(), Not(StatusIs(Ne(absl::StatusCode::kOk))));
  EXPECT_THAT(absl::AbortedError("boom"), StatusIs(Ne(absl::StatusCode::kOk), HasSubstr("boom")));

  const absl::StatusOr<int> status_or = absl::UnavailableError("down");
  EXPECT_THAT(status_or, StatusIs(AnyOf(absl::StatusCode::kUnavailable, absl::StatusCode::kDeadlineExceeded)));
  EXPECT_THAT(status_or, Not(StatusIs(absl::StatusCode::kNotFound)));

  {
    const StatusIsMatcher matcher(Ne(absl::StatusCode::kOk), _);
    EXPECT_THAT(
        Describe(matcher), "has a status code that isn't equal to OK, and has an error message that is anything");
    EXPECT_THAT(MatchAndExplain(matcher, absl::OkStatus()), Pair(false, "which has status code OK"));
    EXPECT_THAT(MatchAndExplain(matcher, absl::AbortedError("x")), Pair(true, ""));
  }
}

TEST_F(StatusMatcherTest, IsOkAndHolds) {
  {
    absl::StatusOr<int> test;
    EXPECT_THAT(test, Not(IsOkAndHolds(_)));
    test = 42;
    EXPECT_THAT(test, IsOkAndHolds(42));
    EXPECT_THAT(test, IsOkAndHolds(Not(25)));
    test = absl::UnknownError("");
    EXPECT_THAT(test, Not(IsOkAndHolds(_)));
  }
  {
    const ::testing::Matcher<absl::StatusOr<int>> matcher = IsOkAndHoldsMatcher<int>(42);
    EXPECT_THAT(Describe(matcher), "is OK and has a value that is equal to 42");
    EXPECT_THAT(DescribeNegation(matcher), "isn't OK or has a value that isn't equal to 42");
    EXPECT_THAT(MatchAndExplain(matcher, absl::StatusOr<int>{42}), Pair(true, ""));
    EXPECT_THAT(MatchAndExplain(matcher, absl::StatusOr<int>{25}), Pair(false, "which contains value 25"));
    EXPECT_THAT(MatchAndExplain(matcher, absl::UnknownError("")), Pair(false, "which has status UNKNOWN"));
  }
  {
    const ::testing::Matcher<absl::StatusOr<int>> matcher = IsOkAndHolds(Ge(42));
    EXPECT_THAT(Describe(matcher), "is OK and has a value that is >= 42");
    EXPECT_THAT(DescribeNegation(matcher), "isn't OK or has a value that isn't >= 42");
    EXPECT_THAT(MatchAndExplain(matcher, absl::StatusOr<int>{42}), Pair(true, ""));
    EXPECT_THAT(MatchAndExplain(matcher, absl::StatusOr<int>{25}), Pair(false, "which contains value 25"));
    EXPECT_THAT(MatchAndExplain(matcher, absl::UnknownError("")), Pair(false, "which has status UNKNOWN"));
  }
  {
    const ::testing::Matcher<absl::StatusOr<std::string>> matcher = IsOkAndHolds(IsEmpty());
    EXPECT_THAT(
        MatchAndExplain(matcher, absl::StatusOr<std::string>{"25"}),
        Pair(false, "which contains value \"25\", whose size is 2"));
  }
}

TEST_F(StatusMatcherTest, AssertOkAndAssign) {
  const absl::StatusOr<std::pair<int, int>> status_or(std::make_pair(25, 17));
  MBO_ASSERT_OK_AND_ASSIGN(auto res, status_or);
  EXPECT_THAT(res, Pair(25, 17));
}

TEST_F(StatusMatcherTest, AssertOkAndAssignTo) {
  const absl::StatusOr<std::pair<int, int>> status_or(std::make_pair(25, 17));
  MBO_ASSERT_OK_AND_MOVE_TO(status_or, auto [first, second]);
  EXPECT_THAT(std::make_pair(first, second), Pair(25, 17));
}

TEST_F(StatusMatcherTest, StatusHasPayload) {
  {
    EXPECT_THAT(absl::OkStatus(), Not(StatusHasPayload()));
    absl::Status status = absl::CancelledError();
    EXPECT_THAT(status, Not(StatusHasPayload()));
    status.SetPayload("url", absl::Cord("content"));
    EXPECT_THAT(status, StatusHasPayload());
    EXPECT_THAT(status, StatusHasPayload("url"));
    EXPECT_THAT(status, Not(StatusHasPayload("other")));
    EXPECT_THAT(status, StatusHasPayload("url", "content"));
    EXPECT_THAT(status, Not(StatusHasPayload("other", "")));
    EXPECT_THAT(status, StatusHasPayload("url", Not("other")));
    EXPECT_THAT(status, StatusHasPayload("url", HasSubstr("tent")));
    EXPECT_THAT(status, Not(StatusHasPayload("url", "other")));
  }
  {
    EXPECT_THAT(absl::StatusOr<int>(1), Not(StatusHasPayload()));
    EXPECT_THAT(absl::StatusOr<int>(absl::CancelledError()), Not(StatusHasPayload()));
    const absl::StatusOr<int> status_or = [] {
      absl::Status status = absl::CancelledError();
      status.SetPayload("url", absl::Cord("content"));
      return status;
    }();
    EXPECT_THAT(status_or, StatusHasPayload());
    EXPECT_THAT(status_or, StatusHasPayload("url"));
  }
}

TEST_F(StatusMatcherTest, MessagesOfStatusHasPayload) {
  const absl::Status status_url_content = [] {
    absl::Status status = absl::CancelledError();
    status.SetPayload("url", absl::Cord("content"));
    return status;
  }();
  const absl::Status status_url_other = [] {
    absl::Status status = absl::CancelledError();
    status.SetPayload("url", absl::Cord("other"));
    return status;
  }();
  const absl::Status status_abc_content = [] {
    absl::Status status = absl::CancelledError();
    status.SetPayload("abc", absl::Cord("content"));
    status.SetPayload("def", absl::Cord("content"));
    return status;
  }();
  const absl::Status status_multiple = [] {
    absl::Status status = absl::CancelledError();
    status.SetPayload("alpha", absl::Cord("one"));
    status.SetPayload("beta", absl::Cord("two"));
    status.SetPayload("gamma", absl::Cord("three"));
    return status;
  }();
  {
    const ::testing::Matcher<absl::Status> matcher = StatusHasPayload();
    EXPECT_THAT(Describe(matcher), "has any payload");
    EXPECT_THAT(DescribeNegation(matcher), "has no payload");
    EXPECT_THAT(MatchAndExplain(matcher, absl::OkStatus()), Pair(false, "which has OK status (and no payload)"));
    EXPECT_THAT(MatchAndExplain(matcher, absl::CancelledError()), Pair(false, "which has no payload"));
    EXPECT_THAT(MatchAndExplain(matcher, status_url_content), Pair(true, "which has 1 payload"));
    EXPECT_THAT(MatchAndExplain(matcher, status_abc_content), Pair(true, "which has 2 payloads"));
  }
  {
    const ::testing::Matcher<absl::StatusOr<int>> matcher = StatusHasPayload();
    EXPECT_THAT(Describe(matcher), "has any payload");
    EXPECT_THAT(DescribeNegation(matcher), "has no payload");
  }
  {
    const ::testing::Matcher<absl::Status> matcher = StatusHasPayload("url");
    EXPECT_THAT(Describe(matcher), "has a payload at url 'url'");
    EXPECT_THAT(DescribeNegation(matcher), "has no payload at url 'url'");
    EXPECT_THAT(MatchAndExplain(matcher, absl::OkStatus()), Pair(false, "which has OK status (and no payload)"));
    EXPECT_THAT(MatchAndExplain(matcher, absl::CancelledError()), Pair(false, "which has no payload"));
    EXPECT_THAT(MatchAndExplain(matcher, status_url_content), Pair(true, "which has a payload at url 'url'"));
    EXPECT_THAT(MatchAndExplain(matcher, status_url_other), Pair(true, "which has a payload at url 'url'"));
    EXPECT_THAT(MatchAndExplain(matcher, status_abc_content), Pair(false, "which has 2 payloads"));
  }
  {
    const ::testing::Matcher<absl::Status> matcher = StatusHasPayload("url", "content");
    EXPECT_THAT(Describe(matcher), "has a payload at url 'url' that is equal to \"content\"");
    EXPECT_THAT(DescribeNegation(matcher), "has no payload at url 'url' or one that isn't equal to \"content\"");
    EXPECT_THAT(MatchAndExplain(matcher, absl::OkStatus()), Pair(false, "which has OK status (and no payload)"));
    EXPECT_THAT(MatchAndExplain(matcher, absl::CancelledError()), Pair(false, "which has no payload"));
    EXPECT_THAT(MatchAndExplain(matcher, status_url_content), Pair(true, "which has a matching payload at url 'url'"));
    EXPECT_THAT(
        MatchAndExplain(matcher, status_url_other), Pair(false, "which has a non-matching payload at url 'url'"));
    EXPECT_THAT(MatchAndExplain(matcher, status_abc_content), Pair(false, "which has 2 payloads"));
  }
  {
    const ::testing::Matcher<absl::Status> matcher = StatusHasPayload("url", HasSubstr("tent"));
    EXPECT_THAT(Describe(matcher), "has a payload at url 'url' that has substring \"tent\"");
    EXPECT_THAT(DescribeNegation(matcher), "has no payload at url 'url' or one that has no substring \"tent\"");
    EXPECT_THAT(MatchAndExplain(matcher, absl::OkStatus()), Pair(false, "which has OK status (and no payload)"));
    EXPECT_THAT(MatchAndExplain(matcher, absl::CancelledError()), Pair(false, "which has no payload"));
    EXPECT_THAT(MatchAndExplain(matcher, status_url_content), Pair(true, "which has a matching payload at url 'url'"));
    EXPECT_THAT(
        MatchAndExplain(matcher, status_url_other), Pair(false, "which has a non-matching payload at url 'url'"));
    EXPECT_THAT(MatchAndExplain(matcher, status_abc_content), Pair(false, "which has 2 payloads"));
  }
  {
    // Verify that selecting one payload remains correct when iteration continues
    // over other payloads, regardless of their internal iteration order.
    EXPECT_THAT(status_multiple, StatusHasPayload("alpha", "one"));
    EXPECT_THAT(status_multiple, StatusHasPayload("beta", "two"));
    EXPECT_THAT(status_multiple, StatusHasPayload("gamma", "three"));
  }
  {
    const ::testing::Matcher<absl::Status> matcher = StatusHasPayload("url", SizeIs(9));
    EXPECT_THAT(
        MatchAndExplain(matcher, status_url_content),
        Pair(false, "which has a non-matching payload at url 'url' that whose size 7 doesn't match"));
  }
}

TEST_F(StatusMatcherTest, StatusPayloads) {
  {
    EXPECT_THAT(absl::OkStatus(), StatusPayloads(IsEmpty()));
    EXPECT_THAT(absl::OkStatus(), StatusPayloads(SizeIs(0)));
    absl::Status status = absl::CancelledError();
    EXPECT_THAT(status, StatusPayloads(IsEmpty()));
    EXPECT_THAT(status, StatusPayloads(SizeIs(0)));
    status.SetPayload("url", absl::Cord("1"));
    EXPECT_THAT(status, StatusPayloads(SizeIs(1)));
    EXPECT_THAT(status, StatusPayloads(ElementsAre(Pair("url", "1"))));
    EXPECT_THAT(status, StatusPayloads(ElementsAre(Not(Pair("bad", "bad")))));
    EXPECT_THAT(status, StatusPayloads(Not(ElementsAre(Pair("bad", "bad")))));
    EXPECT_THAT(status, Not(StatusPayloads(ElementsAre(Pair("bad", "bad")))));
    status.SetPayload("abc", absl::Cord("2"));
    EXPECT_THAT(status, StatusPayloads(SizeIs(2)));
    EXPECT_THAT(status, StatusPayloads(ElementsAre(Key("abc"), Key("url"))));
    EXPECT_THAT(status, StatusPayloads(ElementsAre(Pair("abc", "2"), Pair("url", "1"))));
    EXPECT_THAT(status, Not(StatusPayloads(ElementsAre(Pair("abc", "2"), Pair("url", "0")))));
  }
  {
    EXPECT_THAT(absl::StatusOr<int>(1), StatusPayloads(IsEmpty()));
    EXPECT_THAT(absl::StatusOr<int>(absl::CancelledError()), StatusPayloads(IsEmpty()));
    const absl::StatusOr<int> status_or = [] {
      absl::Status status = absl::CancelledError();
      status.SetPayload("url", absl::Cord("content"));
      return status;
    }();
    EXPECT_THAT(status_or, StatusPayloads(ElementsAre(Pair("url", "content"))));
  }
}

TEST_F(StatusMatcherTest, MessagesOfStatusPayloads) {
  const absl::Status status_url_content = [] {
    absl::Status status = absl::CancelledError();
    status.SetPayload("url", absl::Cord("content"));
    return status;
  }();
  const absl::Status status_abc_another = [] {
    absl::Status status = absl::CancelledError();
    status.SetPayload("abc", absl::Cord("another"));
    status.SetPayload("url", absl::Cord("content"));
    return status;
  }();
  const absl::Status status_abc_content = [] {
    absl::Status status = absl::CancelledError();
    status.SetPayload("abc", absl::Cord("another"));
    status.SetPayload("def", absl::Cord("texting"));
    return status;
  }();
  {
    const ::testing::Matcher<absl::Status> matcher = StatusPayloads(_);
    EXPECT_THAT(MatchAndExplain(matcher, status_url_content), Pair(true, "which has 1 payload"));
    EXPECT_THAT(MatchAndExplain(matcher, status_abc_content), Pair(true, "which has 2 payloads"));
  }
  {
    const ::testing::Matcher<absl::Status> matcher = StatusPayloads(IsEmpty());
    EXPECT_THAT(Describe(matcher), "has a payloads map that is empty");
    EXPECT_THAT(DescribeNegation(matcher), "has a payloads map that isn't empty");
    EXPECT_THAT(MatchAndExplain(matcher, absl::OkStatus()), Pair(true, "which has OK status (and no payload)"));
    EXPECT_THAT(MatchAndExplain(matcher, absl::CancelledError()), Pair(true, "which has no payload"));
    EXPECT_THAT(
        MatchAndExplain(matcher, status_url_content),
        Pair(false, "which has a non-matching payload map whose size is 1"));
    EXPECT_THAT(
        MatchAndExplain(matcher, status_abc_content),
        Pair(false, "which has a non-matching payload map whose size is 2"));
  }
  {
    const ::testing::Matcher<absl::Status> matcher =
        StatusPayloads(ElementsAre(Pair("abc", "another"), Pair("url", "content")));
    EXPECT_THAT(
        Describe(matcher),
        "has a payloads map that has 2 elements where\n"
        "element #0 has a first field that is equal to \"abc\", and has a second field that is equal to \"another\",\n"
        "element #1 has a first field that is equal to \"url\", and has a second field that is equal to \"content\"");
    EXPECT_THAT(
        DescribeNegation(matcher),
        "has a payloads map that doesn't have 2 elements, or\n"
        "element #0 has a first field that isn't equal to \"abc\", or has a second field that isn't equal to "
        "\"another\", or\n"
        "element #1 has a first field that isn't equal to \"url\", or has a second field that isn't equal to "
        "\"content\"");
    EXPECT_THAT(MatchAndExplain(matcher, absl::OkStatus()), Pair(false, "which has OK status (and no payload)"));
    EXPECT_THAT(MatchAndExplain(matcher, absl::CancelledError()), Pair(false, "which has no payload"));
    EXPECT_THAT(
        MatchAndExplain(matcher, status_url_content),
        Pair(false, "which has a non-matching payload map which has 1 element"));
    EXPECT_THAT(
        MatchAndExplain(matcher, status_abc_content),
        Pair(
            false,
            "which has a non-matching payload map whose element #1 ((\"def\", \"texting\")) has a first field that "
            "isn't equal to \"url\", or has a second field that isn't equal to \"content\", whose first field does not "
            "match"));
    EXPECT_THAT(
        MatchAndExplain(matcher, status_abc_another),
        Pair(
            true,
            "which has a matching payload map whose element #0 matches, whose both fields match,\n"
            "and whose element #1 matches, whose both fields match"));
  }
}

// NOLINTEND(readability-magic-numbers)
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

}  // namespace
}  // namespace mbo::testing
