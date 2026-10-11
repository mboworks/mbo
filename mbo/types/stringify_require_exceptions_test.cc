// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <stdexcept>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/config/config.h"
#include "mbo/types/stringify.h"

namespace mbo::types {
namespace {
using ::testing::HasSubstr;
using ::testing::IsTrue;
using ::testing::ThrowsMessage;

struct StringifyRequireExceptionsTest : ::testing::Test {
 protected:
  void SetUp() override {
    if constexpr (!config::kRequireThrows) {
      GTEST_SKIP() << "Requires require_throws and compiler exceptions";
    }
  }
};

TEST_F(StringifyRequireExceptionsTest, IncompleteOptionsThrow) {
  const StringifyOptions incomplete;
  static_assert(noexcept(Stringify(incomplete)) == !config::kRequireThrows);
  EXPECT_THAT([&] { return Stringify(incomplete); }, ThrowsMessage<std::runtime_error>(HasSubstr("Not all data set:")));
}

TEST_F(StringifyRequireExceptionsTest, CompleteOptionsRemainUsable) {
  const Stringify stringify;

  struct Value {
    int value;
  };

  EXPECT_THAT(stringify.ToString(Value{.value = 1}), HasSubstr("1"));
}

TEST_F(StringifyRequireExceptionsTest, BuiltInOptionsAreCompleteAndNoexcept) {
  static_assert(noexcept(Stringify::OptionsDefault()));
  static_assert(noexcept(Stringify::OptionsDisabled()));
  static_assert(noexcept(Stringify::OptionsCpp()));
  static_assert(noexcept(Stringify::OptionsCppPretty()));
  static_assert(noexcept(Stringify::OptionsJson()));
  static_assert(noexcept(Stringify::OptionsJsonLine()));
  static_assert(noexcept(Stringify::OptionsJsonPretty()));
  static_assert(Stringify::OptionsDefault().AllDataSet());
  static_assert(Stringify::OptionsDisabled().AllDataSet());
  static_assert(Stringify::OptionsCpp().AllDataSet());
  static_assert(Stringify::OptionsCppPretty().AllDataSet());
  static_assert(Stringify::OptionsJson().AllDataSet());
  static_assert(Stringify::OptionsJsonLine().AllDataSet());
  static_assert(Stringify::OptionsJsonPretty().AllDataSet());
  for (const auto mode : {
           Stringify::OutputMode::kDefault,
           Stringify::OutputMode::kCpp,
           Stringify::OutputMode::kCppPretty,
           Stringify::OutputMode::kJson,
           Stringify::OutputMode::kJsonLine,
           Stringify::OutputMode::kJsonPretty,
       }) {
    EXPECT_THAT(Stringify::OptionsAs(mode).AllDataSet(), IsTrue());
  }
}
}  // namespace
}  // namespace mbo::types
