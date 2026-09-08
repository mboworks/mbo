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

#include "mbo/config/config.h"

#include <concepts>
#include <cstddef>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::config {
namespace {

struct ConfigTest : ::testing::Test {};

TEST_F(ConfigTest, ExposesTheGeneratedConfigValues) {
  // The header either includes the generated `config_gen.h` or falls back to the
  // template. Either way these must exist with these types - consumers depend on
  // them at compile time (LimitedOrdered static_asserts on kUnrollMaxCapacityDefault).
  static_assert(std::same_as<decltype(kUnrollMaxCapacityDefault), const std::size_t>);
  static_assert(std::same_as<decltype(kRequireThrows), const bool>);
}

TEST_F(ConfigTest, UnrollCapacityIsWithinTheRangeItsConsumersRequire) {
  // `LimitedOrdered` static_asserts `>= 4 && <= 32`; if the generated value ever
  // leaves that window the failure appears deep inside a container instantiation
  // rather than here.
  static_assert(kUnrollMaxCapacityDefault >= 4);
  static_assert(kUnrollMaxCapacityDefault <= 32);
}

TEST_F(ConfigTest, ValuesAreUsableInAConstantExpression) {
  // They are consumed as template arguments, so they must be true constants and not
  // merely const variables.
  constexpr std::size_t kCapacity = kUnrollMaxCapacityDefault;
  constexpr bool kThrows = kRequireThrows;
  static_assert(kCapacity == kUnrollMaxCapacityDefault);
  static_assert(kThrows == kRequireThrows);
}

TEST_F(ConfigTest, Constexpr23MacroIsDefined) {
  // `MBO_CONFIG_CONSTEXPR_23` expands to `constexpr` when the selected language mode supports the
  // required constant evaluation and to nothing otherwise; either way it must be defined, since
  // declarations use it bare.
#ifndef MBO_CONFIG_CONSTEXPR_23
  FAIL() << "MBO_CONFIG_CONSTEXPR_23 is not defined";
#endif
  // It must also be usable in a declaration under either standard.
  MBO_CONFIG_CONSTEXPR_23 const int value = 42;  // NOLINT(*-magic-numbers)
  EXPECT_THAT(value, 42);                        // NOLINT(*-magic-numbers)
}

}  // namespace
}  // namespace mbo::config
