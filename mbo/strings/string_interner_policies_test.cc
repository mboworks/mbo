// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/string_interner_policies.h"

#include <cstdint>
#include <string>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::strings {
namespace {

using ::testing::Eq;

struct StringInternerPoliciesTest : ::testing::Test {};

TEST_F(StringInternerPoliciesTest, ErrorPoliciesExposeTheirDeclaredResults) {
  using Id = std::uint32_t;
  static_assert(
      std::same_as<
          StringInternerErrorStlPolicy::insertion_result<Id>, std::expected<std::pair<Id, bool>, StringInternerError>>);
  static_assert(
      std::same_as<StringInternerErrorStatusPolicy::insertion_result<Id>, absl::StatusOr<std::pair<Id, bool>>>);
  static_assert(
      std::same_as<StringInternerErrorOptionalPolicy::insertion_result<Id>, std::optional<std::pair<Id, bool>>>);
  EXPECT_THAT(StringInternerErrorOptionalPolicy::Success<Id>(7, true), Eq(std::pair<Id, bool>{7, true}));
  EXPECT_THAT(StringInternerErrorOptionalPolicy::Failure<Id>(StringInternerError::kIdExhausted), Eq(std::nullopt));
}

TEST_F(StringInternerPoliciesTest, LookupPoliciesPreserveReferences) {
  std::string value = "value";
  auto mbo_result = StringInternerLookupMboPolicy::Make(value);
  auto stl_result = StringInternerLookupStlPolicy::Make(value);
  EXPECT_THAT(StringInternerLookupMboPolicy::Get(mbo_result), Eq("value"));
  EXPECT_THAT(StringInternerLookupStlPolicy::Get(stl_result), Eq("value"));
  StringInternerLookupStlPolicy::Get(stl_result) = "changed";
  EXPECT_THAT(value, Eq("changed"));
}

TEST_F(StringInternerPoliciesTest, ExceptionPoliciesSelectRollbackMode) {
  static_assert(!StringInternerExceptionFreePolicy::kRollbackOnException);
  static_assert(StringInternerExceptionSafePolicy::kRollbackOnException);
  static_assert(!StringInternerPolicyHolder<>::exception_policy::kRollbackOnException);
  static_assert(StringInternerPolicyHolder<
                StringInternerErrorStlPolicy, StringInternerLookupMboPolicy, StringInternerIndexStlPolicy,
                StringInternerExceptionSafePolicy>::exception_policy::kRollbackOnException);
  EXPECT_THAT(StringInternerExceptionFreePolicy::kRollbackOnException, Eq(false));
  EXPECT_THAT(StringInternerExceptionSafePolicy::kRollbackOnException, Eq(true));
}

}  // namespace
}  // namespace mbo::strings
