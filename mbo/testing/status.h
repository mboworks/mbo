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

#ifndef MBO_TESTING_STATUS_H_
#define MBO_TESTING_STATUS_H_

#include <optional>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status.h"
#include "mbo/status/status_macros.h"

namespace mbo::testing {
namespace testing_internal {

inline std::string DescribeStatus(absl::Status status) {
  std::string code = ::testing::PrintToString(status.code());
  if (status.message().empty()) {
    return code;
  } else {
    return absl::StrCat(code, ": '", status.message(), "'");
  }
}

// Monomorphic implementation of matcher IsOk() for a given type T.
// T can be Status, StatusOr<>, or a reference to either of them.
template<typename T>
class MonoIsOkMatcherImpl : public ::testing::MatcherInterface<T> {
 public:
  MonoIsOkMatcherImpl() = default;

  // LCOV_MERGE_FUNC_LINE: repeated for every supported status-bearing type.
  void DescribeTo(std::ostream* os) const override { *os << "is OK"; }

  // LCOV_MERGE_FUNC_LINE: repeated for every supported status-bearing type.
  void DescribeNegationTo(std::ostream* os) const override { *os << "is not OK"; }

  bool MatchAndExplain(T actual_value, ::testing::MatchResultListener* result) const override {
    const absl::Status status = ::mbo::status::GetStatus(actual_value);
    if (!status.ok()) {
      *result << "which has status " << DescribeStatus(status);
    }
    return status.ok();
  }
};

// Implements IsOk() as a polymorphic matcher.
class IsOkMatcher {
 public:
  IsOkMatcher() = default;

  template<typename T>
  operator ::testing::Matcher<T>() const {  // NOLINT
    return ::testing::Matcher<T>(new MonoIsOkMatcherImpl<T>());
  }
};

// Monomorphic implementation of matcher IsOkAndHolds(m).  StatusOrType is a
// reference to StatusOr<T>.
template<typename StatusOrType>
class IsOkAndHoldsMatcherImpl : public ::testing::MatcherInterface<StatusOrType> {
 public:
  // NOLINTNEXTLINE(readability-identifier-naming)
  using value_type = std::remove_reference_t<StatusOrType>::value_type;

  template<typename InnerMatcher>
  explicit IsOkAndHoldsMatcherImpl(InnerMatcher&& inner_matcher)
  requires(!std::is_same_v<InnerMatcher, IsOkAndHoldsMatcherImpl>)
      : inner_matcher_(::testing::SafeMatcherCast<const value_type&>(std::forward<InnerMatcher>(inner_matcher))) {}

  // LCOV_MERGE_FUNC_LINE: repeated for every StatusOr value and matcher type.
  void DescribeTo(std::ostream* os) const override {
    *os << "is OK and has a value that ";
    inner_matcher_.DescribeTo(os);
  }

  // LCOV_MERGE_FUNC_LINE: repeated for every StatusOr value and matcher type.
  void DescribeNegationTo(std::ostream* os) const override {
    *os << "isn't OK or has a value that ";
    inner_matcher_.DescribeNegationTo(os);
  }

  bool MatchAndExplain(StatusOrType actual_value, ::testing::MatchResultListener* result_listener) const override {
    if (!actual_value.ok()) {
      *result_listener << "which has status " << DescribeStatus(actual_value.status());
      return false;
    }

    ::testing::StringMatchResultListener inner;
    const bool matches = inner_matcher_.MatchAndExplain(*actual_value, &inner);
    if (!inner.str().empty()) {
      *result_listener << "which contains value " << ::testing::PrintToString(*actual_value) << ", " << inner.str();
    } else if (!matches) {
      *result_listener << "which contains value " << ::testing::PrintToString(*actual_value);
    }
    return matches;
  }

 private:
  const ::testing::Matcher<const value_type&> inner_matcher_;
};

// Implements IsOkAndHolds(m) as a polymorphic matcher.
template<typename InnerMatcher>
class IsOkAndHoldsMatcher {
 public:
  // The canonical sink: take by value, move into place. The check cannot see the
  // move when InnerMatcher is a copy-only gmock composite (VariadicMatcher has no
  // move constructor), and taking const& instead would force that copy on movable
  // matchers too.
  // NOLINTNEXTLINE(performance-unnecessary-value-param)
  explicit IsOkAndHoldsMatcher(InnerMatcher inner_matcher) : inner_matcher_(std::move(inner_matcher)) {}

  // Converts this polymorphic matcher to a monomorphic matcher of the
  // given type.  StatusOrType can be either StatusOr<T> or a
  // reference to StatusOr<T>.
  template<typename StatusOrType>
  operator ::testing::Matcher<StatusOrType>() const {  // NOLINT
    return ::testing::Matcher<StatusOrType>(new IsOkAndHoldsMatcherImpl<const StatusOrType&>(inner_matcher_));
  }

 private:
  const InnerMatcher inner_matcher_;
};

// `StatusCode` is implicitly convertible from `int` and `absl::StatusCode` so
// that `StatusIs()` accepts either a literal status code or a gMock matcher over
// status codes (e.g. `StatusIs(AnyOf(kUnknown, kCancelled))`). Adapted from
// Abseil's `absl::status_internal::StatusCode`.
class StatusCode {
 public:
  StatusCode(int code) : code_(static_cast<absl::StatusCode>(code)) {}  // NOLINT

  StatusCode(absl::StatusCode code) : code_(code) {}  // NOLINT

  explicit operator int() const { return static_cast<int>(code_); }

  friend bool operator==(const StatusCode& lhs, const StatusCode& rhs) { return lhs.code_ == rhs.code_; }

  friend bool operator!=(const StatusCode& lhs, const StatusCode& rhs) { return lhs.code_ != rhs.code_; }

  friend void PrintTo(const StatusCode& code, std::ostream* os) { *os << absl::StatusCodeToString(code.code_); }

 private:
  absl::StatusCode code_;
};

// Implements a status matcher interface to verify that a status code (matched
// against an `absl::StatusCode` or a code matcher) and the error message (if
// set) match expected values.
//
// Sample usage:
//   EXPECT_THAT(MyCall(), StatusIs(absl::StatusCode::kNotFound, HasSubstr("message")));
//   EXPECT_THAT(MyCall(), StatusIs(AnyOf(absl::StatusCode::kNotFound, absl::StatusCode::kUnavailable)));
class StatusIsMatcher {
 public:
  template<typename StatusCodeMatcher, typename MessageMatcher>
  StatusIsMatcher(StatusCodeMatcher&& code_matcher, MessageMatcher&& message_matcher)
      : code_matcher_(::testing::MatcherCast<StatusCode>(std::forward<StatusCodeMatcher>(code_matcher))),
        message_matcher_(::testing::MatcherCast<const std::string&>(std::forward<MessageMatcher>(message_matcher))) {}

  void DescribeTo(std::ostream* os) const {
    *os << "has a status code that ";
    code_matcher_.DescribeTo(os);
    *os << ", and has an error message that ";
    message_matcher_.DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "not (";
    DescribeTo(os);
    *os << ")";
  }

  template<typename StatusType>
  bool MatchAndExplain(const StatusType& actual, ::testing::MatchResultListener* listener) const {
    const absl::Status& actual_status = ::mbo::status::GetStatus(actual);
    const bool code_match = code_matcher_.Matches(StatusCode{actual_status.code()});
    if (actual_status.code() == absl::StatusCode::kOk && code_match) {
      return true;  // An OK status carries no message; ignore the message matcher.
    }
    ::testing::StringMatchResultListener inner;
    const bool message_match = message_matcher_.MatchAndExplain(std::string{actual_status.message()}, &inner);
    if (code_match && message_match) {
      return true;
    }
    *listener << "which has status code " << actual_status.code();
    if (actual_status.code() != absl::StatusCode::kOk) {
      *listener << " and ";
      if (message_match) {
        *listener << "a matching message";
      } else if (actual_status.message().empty()) {
        *listener << "an empty message";
      } else {
        *listener << "the message '" << actual_status.message() << "'";
        if (!inner.str().empty()) {
          *listener << " (" << inner.str() << ")";
        }
      }
    }
    return false;
  }

 private:
  const ::testing::Matcher<StatusCode> code_matcher_;
  const ::testing::Matcher<const std::string&> message_matcher_;
};

// Implementation of `StatusHasPayload` matcher.
class StatusHasPayload {
 public:
  StatusHasPayload() : type_url_(std::nullopt), payload_matcher_(std::nullopt) {}

  explicit StatusHasPayload(std::string_view type_url) : type_url_(type_url), payload_matcher_(std::nullopt) {}

  StatusHasPayload(std::string_view type_url, const ::testing::Matcher<const std::string&>& payload_matcher)
      : type_url_(type_url), payload_matcher_(payload_matcher) {}

  void DescribeTo(std::ostream* os) const {
    if (type_url_.has_value()) {
      *os << "has a payload at url '" << *type_url_ << "'";
      if (payload_matcher_.has_value()) {
        *os << " that ";
        payload_matcher_->DescribeTo(os);
      }
    } else {
      *os << "has any payload";
    }
  }

  void DescribeNegationTo(std::ostream* os) const {
    if (type_url_.has_value()) {
      if (payload_matcher_.has_value()) {
        *os << "has no payload at url '" << *type_url_ << "' or one that ";
        payload_matcher_->DescribeNegationTo(os);
      } else {
        *os << "has no payload at url '" << *type_url_ << "'";
      }
    } else {
      *os << "has no payload";
    }
  }

  template<typename StatusType>
  bool MatchAndExplain(const StatusType& actual, ::testing::MatchResultListener* listener) const {
    const absl::Status& actual_status = ::mbo::status::GetStatus(actual);
    if (actual_status.ok()) {
      *listener << "which has OK status (and no payload)";
      return false;
    }
    PayloadInfo info;
    actual_status.ForEachPayload([this, &info, listener](std::string_view type_url, const absl::Cord& payload) {
      ++info.count;
      if (info.messaged) {
        return;
      }
      if (!type_url_.has_value()) {
        info.match = true;
      } else if (*type_url_ == type_url) {
        if (payload_matcher_.has_value()) {
          ::testing::StringMatchResultListener inner;
          info.match = payload_matcher_->MatchAndExplain(std::string{payload}, &inner);
          info.messaged = true;
          if (info.match) {
            *listener << "which has a matching payload at url '" << type_url << "'";
          } else {
            *listener << "which has a non-matching payload at url '" << type_url << "'";
          }
          if (!inner.str().empty()) {
            *listener << " that " << inner.str();
          }
        } else {
          info.match = true;
          info.messaged = true;
          *listener << "which has a payload at url '" << type_url << "'";
        }
      }
    });
    if (info.messaged) {
      return info.match;
    }
    if (info.count == 0) {
      *listener << "which has no payload";
    } else if (info.count == 1) {
      *listener << "which has " << info.count << " payload";
    } else {
      *listener << "which has " << info.count << " payloads";
    }
    return info.match;
  }

 private:
  struct PayloadInfo {
    uint64_t count = 0;
    bool match = false;
    bool messaged = false;
  };

  const std::optional<std::string> type_url_;
  const std::optional<::testing::Matcher<const std::string&>> payload_matcher_;
};

// Implementation of `StatusPayloads` matcher.
class StatusPayloads {
 public:
  explicit StatusPayloads(const ::testing::Matcher<const absl::btree_map<std::string, std::string>&>& payload_matcher)
      : payload_matcher_(payload_matcher) {}

  void DescribeTo(std::ostream* os) const {
    *os << "has a payloads map that ";
    payload_matcher_.DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "has a payloads map that ";
    payload_matcher_.DescribeNegationTo(os);
  }

  template<typename StatusType>
  bool MatchAndExplain(const StatusType& actual, ::testing::MatchResultListener* listener) const {
    const absl::Status& actual_status = ::mbo::status::GetStatus(actual);
    absl::btree_map<std::string, std::string> payload_map;
    actual_status.ForEachPayload([&payload_map](std::string_view type_url, const absl::Cord& payload) {
      payload_map.emplace(type_url, payload);
    });
    ::testing::StringMatchResultListener inner;
    const bool match = payload_matcher_.MatchAndExplain(payload_map, &inner);
    if (inner.str().empty()) {
      if (actual_status.ok()) {
        *listener << "which has OK status (and no payload)";
      } else {
        if (payload_map.empty()) {
          *listener << "which has no payload";
        } else if (payload_map.size() == 1) {
          *listener << "which has 1 payload";
        } else {
          *listener << "which has " << payload_map.size() << " payloads";
        }
      }
    } else {
      if (match) {
        *listener << "which has a matching payload map " << inner.str();
      } else {
        *listener << "which has a non-matching payload map " << inner.str();
      }
    }
    return match;
  }

 private:
  const ::testing::Matcher<const absl::btree_map<std::string, std::string>&> payload_matcher_;
};

}  // namespace testing_internal

// Returns a gMock matcher that matches a Status or StatusOr<> which is OK.
inline testing_internal::IsOkMatcher IsOk() {
  return {};
}

// Returns a gMock matcher that matches a StatusOr<> whose status is
// OK and whose value matches the inner matcher.
template<typename InnerMatcher>
testing_internal::IsOkAndHoldsMatcher<std::decay_t<InnerMatcher>> IsOkAndHolds(InnerMatcher&& inner_matcher) {
  return testing_internal::IsOkAndHoldsMatcher<std::decay_t<InnerMatcher>>(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
      std::forward<InnerMatcher>(inner_matcher));
}

// Status matcher that checks the status code against `code_matcher`, which may be
// a literal `absl::StatusCode` or a gMock matcher over status codes.
template<typename StatusCodeMatcher>
::testing::PolymorphicMatcher<testing_internal::StatusIsMatcher> StatusIs(StatusCodeMatcher&& code_matcher) {
  return ::testing::MakePolymorphicMatcher(
      testing_internal::StatusIsMatcher(std::forward<StatusCodeMatcher>(code_matcher), ::testing::_));
}

// Status matcher that checks both the status code (literal or matcher) and the
// message against the given matchers.
template<typename StatusCodeMatcher, typename MessageMatcher>
::testing::PolymorphicMatcher<testing_internal::StatusIsMatcher> StatusIs(
    StatusCodeMatcher&& code_matcher,
    MessageMatcher&& message_matcher) {
  return ::testing::MakePolymorphicMatcher(
      testing_internal::StatusIsMatcher(
          std::forward<StatusCodeMatcher>(code_matcher), std::forward<MessageMatcher>(message_matcher)));
}

// Returns a gMock matcher that matches a Status/StatusOr<> whose status is NOT
// OK and that has at least one payload.
inline ::testing::PolymorphicMatcher<testing_internal::StatusHasPayload> StatusHasPayload() {
  return ::testing::MakePolymorphicMatcher(testing_internal::StatusHasPayload());
}

// Returns a gMock matcher that matches a Status/StatusOr<> whose status is NOT
// OK and that has a payload at `type_url`.
inline ::testing::PolymorphicMatcher<testing_internal::StatusHasPayload> StatusHasPayload(std::string_view type_url) {
  return ::testing::MakePolymorphicMatcher(testing_internal::StatusHasPayload(type_url));
}

// Returns a gMock matcher that matches a Status/StatusOr<> whose status is NOT
// OK and that has a payload at `type_url` that matches `payload_matcher`.
template<typename PayloadMatcher>
inline ::testing::PolymorphicMatcher<testing_internal::StatusHasPayload> StatusHasPayload(
    std::string_view type_url,
    const PayloadMatcher& payload_matcher) {
  return ::testing::MakePolymorphicMatcher(
      testing_internal::StatusHasPayload(type_url, ::testing::MatcherCast<const std::string&>(payload_matcher)));
}

// Returns gMock matcher that matches against Status/StatusOr<> payload maps.
// Unlike `StatusHasPayload` here we compare the whole mapping of urls to content.
template<typename PayloadMatcher>
inline ::testing::PolymorphicMatcher<testing_internal::StatusPayloads> StatusPayloads(
    const PayloadMatcher& payload_matcher) {
  return ::testing::MakePolymorphicMatcher(
      testing_internal::StatusPayloads(
          ::testing::MatcherCast<const absl::btree_map<std::string, std::string>&>(payload_matcher)));
}

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// NOLINTBEGIN(bugprone-macro-parentheses)

// Helper macros
#undef MBO_PRIVATE_TESTING_STATUS_CONCAT_INNER_
#undef MBO_PRIVATE_TESTING_STATUS_CONCAT_
#define MBO_PRIVATE_TESTING_STATUS_CONCAT_INNER_(x, y) x##y
#define MBO_PRIVATE_TESTING_STATUS_CONCAT_(x, y) MBO_PRIVATE_TESTING_STATUS_CONCAT_INNER_(x, y)

// Macros for testing the results of functions that return absl::Status or
// absl::StatusOr<T> (for any type T).
#undef MBO_EXPECT_OK
#define MBO_EXPECT_OK(expression) EXPECT_THAT(expression, ::mbo::testing::IsOk())

#if !defined(EXPECT_OK)
# define EXPECT_OK(expr) MBO_EXPECT_OK(expr)
#else
# warning "EXPECT_OK already defined"
#endif  // !defined(EXPECT_OK)

#undef MBO_ASSERT_OK
#define MBO_ASSERT_OK(expression) ASSERT_THAT(expression, ::mbo::testing::IsOk())

#if !defined(ASSERT_OK)
# define ASSERT_OK(expr) MBO_ASSERT_OK(expr)
#else
# warning "ASSERT_OK already defined"
#endif  // !defined(ASSERT_OK)

#undef MBO_PRIVATE_TESTING_STATUS_ASSERT_OK_AND_ASSIGN_
#undef MBO_ASSERT_OK_AND_ASSIGN
#undef MBO_ASSERT_OK_AND_MOVE_TO

// PRIVATE macro, do not use.
#define MBO_PRIVATE_TESTING_STATUS_ASSERT_OK_AND_ASSIGN_(statusor, expression, ...) \
  auto statusor = (expression);                                                     \
  ASSERT_OK(statusor);                                                              \
  __VA_ARGS__ = std::move(statusor).value()

// Macro that verifies `expression` is OK and assigns its `value` by move to `targets`, where target
// can be a declaration. Example:
// ```
// MBO_ASSERT_OK_AND_ASSIGN(const std::string result, StringOrStatus());
// ```
#define MBO_ASSERT_OK_AND_ASSIGN(target, expression) \
  MBO_PRIVATE_TESTING_STATUS_ASSERT_OK_AND_ASSIGN_(  \
      MBO_PRIVATE_TESTING_STATUS_CONCAT_(_status_or_value, __LINE__), expression, target)

// Variant of `MBO_ASSERT_OK_AND_ASSIGN` that allows to assign to complex types, in particular to
// structured bindings. Example:
// ```
// MBO_ASSERT_OK_AND_MOVE_TO(PairOrStatus(), const auto [first, second]);
// ```
#define MBO_ASSERT_OK_AND_MOVE_TO(expression, ...)  \
  MBO_PRIVATE_TESTING_STATUS_ASSERT_OK_AND_ASSIGN_( \
      MBO_PRIVATE_TESTING_STATUS_CONCAT_(_status_or_value, __LINE__), expression, __VA_ARGS__)

#if !defined(ASSERT_OK_AND_ASSIGN)
# define ASSERT_OK_AND_ASSIGN(target, expression) MBO_ASSERT_OK_AND_ASSIGN(target, expression)
#else
# warning "ASSERT_OK_AND_ASSIGN already defined"
#endif  // !defined(ASSERT_OK_AND_ASSIGN)

// NOLINTEND(bugprone-macro-parentheses)
// NOLINTEND(cppcoreguidelines-macro-usage)

}  // namespace mbo::testing

#endif  // MBO_TESTING_STATUS_H_
