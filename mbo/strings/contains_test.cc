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

#include "mbo/strings/contains.h"

#include <string_view>

#include "gtest/gtest.h"

namespace mbo::strings {
namespace {

using ::testing::Test;

class ContainsTest : public Test {};

// The reason this exists at all: `absl::StrContains` cannot appear in a constant
// expression, so these must be usable at compile time.
static_assert(Contains("haystack", "stack"));
static_assert(!Contains("haystack", "needle"));
static_assert(Contains("haystack", 'y'));
static_assert(!Contains("haystack", 'z'));
static_assert(Contains("anything", std::string_view()));
static_assert(!Contains(std::string_view(), 'a'));
// An empty needle is contained in everything, as with absl::StrContains.
static_assert(Contains("haystack", ""));
static_assert(Contains(std::string_view(), ""));
static_assert(!Contains("haystack", '\0'));

TEST_F(ContainsTest, Substring) {
  EXPECT_TRUE(Contains("haystack", "hay"));
  EXPECT_TRUE(Contains("haystack", "stack"));
  EXPECT_TRUE(Contains("haystack", "haystack"));
  EXPECT_FALSE(Contains("haystack", "haystacks"));
  EXPECT_FALSE(Contains("hay", "haystack"));
}

TEST_F(ContainsTest, Char) {
  EXPECT_TRUE(Contains("haystack", 'h'));
  EXPECT_TRUE(Contains("haystack", 'k'));
  EXPECT_FALSE(Contains("haystack", 'Z'));
}

TEST_F(ContainsTest, EmptyNeedleIsAlwaysContained) {
  // Matches absl::StrContains.
  EXPECT_TRUE(Contains("haystack", ""));
  EXPECT_TRUE(Contains("haystack", std::string_view()));
  EXPECT_TRUE(Contains(std::string_view(), ""));
  EXPECT_TRUE(Contains(std::string_view(), std::string_view()));
}

TEST_F(ContainsTest, EmptyHaystack) {
  EXPECT_FALSE(Contains(std::string_view(), 'a'));
  EXPECT_FALSE(Contains(std::string_view(), "a"));
}

TEST_F(ContainsTest, NulIsAnOrdinaryCharacter) {
  // The char overload searches for that character; it is not "empty needle".
  EXPECT_FALSE(Contains("haystack", '\0'));
  EXPECT_TRUE(Contains(std::string_view("a\0b", 3), '\0'));
}

}  // namespace
}  // namespace mbo::strings
