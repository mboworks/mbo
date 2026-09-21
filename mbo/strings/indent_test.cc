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

#include "mbo/strings/indent.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"

namespace mbo::strings {
namespace {

using ::mbo::testing::EqualsText;
using ::testing::ElementsAre;

class DropIndentTest : public ::testing::Test {};

TEST_F(DropIndentTest, Empty) {
  EXPECT_THAT(DropIndent(""), "");
}

TEST_F(DropIndentTest, Single) {
  EXPECT_THAT(DropIndent("abc"), "abc");
  EXPECT_THAT(DropIndent(" abc "), " abc ");
}

TEST_F(DropIndentTest, EmptyLines) {
  EXPECT_THAT(DropIndentAndSplit("\n"), ElementsAre(""));
  EXPECT_THAT(DropIndent("\n"), EqualsText("\n"));
  EXPECT_THAT(DropIndentAndSplit("\n\n"), ElementsAre("", ""));
  EXPECT_THAT(DropIndent("\n\n"), EqualsText("\n"));
  EXPECT_THAT(DropIndentAndSplit("\n \n "), ElementsAre("", ""));
  EXPECT_THAT(DropIndentAndSplit("\n  \n "), ElementsAre("", ""));
  EXPECT_THAT(DropIndentAndSplit("\n \n  "), ElementsAre("", ""));
  EXPECT_THAT(DropIndentAndSplit("\n \n 1\n  "), ElementsAre("", "1", ""));
  EXPECT_THAT(DropIndentAndSplit("\n \n  2\n  "), ElementsAre("", " 2", ""));
  EXPECT_THAT(DropIndentAndSplit("\n 1\n\n 1\n  "), ElementsAre("1", "", "1", ""));
}

TEST_F(DropIndentTest, Basic) {
  EXPECT_THAT(DropIndentAndSplit("\n 1\n  2"), ElementsAre("1", " 2"));
  EXPECT_THAT(DropIndentAndSplit("\n 1\n  2\n"), ElementsAre("1", " 2", ""));
  EXPECT_THAT(DropIndentAndSplit("\n 1\n  2\n  "), ElementsAre("1", " 2", ""));
  EXPECT_THAT(DropIndentAndSplit("\n   3\n  2\n "), ElementsAre("3", "  2", ""))
      << "Second line has shorter indent, so not changing.";
}

TEST_F(DropIndentTest, NonEmptyFirst) {
  EXPECT_THAT(DropIndentAndSplit("a\n"), ElementsAre("a", "")) << "Keep first line content.";
  EXPECT_THAT(DropIndentAndSplit(" a\n 1"), ElementsAre(" a", "1")) << "Keep first line content.";
  EXPECT_THAT(DropIndentAndSplit("a\n 1\n  2\n 1\n"), ElementsAre("a", "1", " 2", "1", ""))
      << "Keep first line content.";
}

TEST_F(DropIndentTest, LastLineIndent) {
  EXPECT_THAT(DropIndentAndSplit("\n 1\n 1"), ElementsAre("1", "1"));
  EXPECT_THAT(DropIndentAndSplit("\n 1\n  2"), ElementsAre("1", " 2"));
  EXPECT_THAT(DropIndentAndSplit("\n  2\n  2"), ElementsAre("2", "2"));
  EXPECT_THAT(DropIndentAndSplit("\n 1\n  2\n   "), ElementsAre("1", " 2", ""))
      << "Clear last line if only whitespace.";
  EXPECT_THAT(DropIndentAndSplit("\n  2\n  2\n 1"), ElementsAre("2", "2", " 1"))
      << "Last line has shorter whitespace, keep it.";
}

}  // namespace
}  // namespace mbo::strings
