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

#include "mbo/strings/strip.h"

#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"

namespace mbo::strings {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Pair;

struct StripTest : public ::testing::Test {};

TEST_F(StripTest, ConsumePrefix) {
  auto test_prefix = [](std::string text, std::string_view prefix) {
    const std::string stripped = StripPrefix(std::string{text}, prefix);
    const bool consumed = ConsumePrefix(text, prefix);
    EXPECT_THAT(stripped, text);
    return std::pair<bool, std::string>{consumed, text};
  };
  EXPECT_THAT(test_prefix("", ""), Pair(true, ""));
  EXPECT_THAT(test_prefix("", "foo"), Pair(false, ""));
  EXPECT_THAT(test_prefix("foo", "foo"), Pair(true, ""));
  EXPECT_THAT(test_prefix("foo", "oo"), Pair(false, "foo"));
  EXPECT_THAT(test_prefix("foo", ""), Pair(true, "foo"));
  EXPECT_THAT(test_prefix("foo", "fooo"), Pair(false, "foo"));
  EXPECT_THAT(test_prefix("foobar", "foo"), Pair(true, "bar"));
}

TEST_F(StripTest, ConsumeSuffix) {
  const auto test_suffix = [](std::string text, std::string_view suffix) {
    const std::string stripped = StripSuffix(std::string{text}, suffix);
    const bool consumed = ConsumeSuffix(text, suffix);
    EXPECT_THAT(stripped, text);
    return std::pair<bool, std::string>{consumed, text};
  };
  EXPECT_THAT(test_suffix("", ""), Pair(true, ""));
  EXPECT_THAT(test_suffix("", "foo"), Pair(false, ""));
  EXPECT_THAT(test_suffix("foo", "foo"), Pair(true, ""));
  EXPECT_THAT(test_suffix("foo", "f"), Pair(false, "foo"));
  EXPECT_THAT(test_suffix("foo", ""), Pair(true, "foo"));
  EXPECT_THAT(test_suffix("foo", "fooo"), Pair(false, "foo"));
  EXPECT_THAT(test_suffix("foobar", "bar"), Pair(true, "foo"));
}

TEST_F(StripTest, Simple) {
  EXPECT_THAT(StripComments("", {.comment_start = "#"}), "");
  EXPECT_THAT(StripComments("#", {.comment_start = "#"}), "");
  EXPECT_THAT(StripComments("##", {.comment_start = "#"}), "");
  EXPECT_THAT(StripComments("#", {.comment_start = "##"}), "#");
  EXPECT_THAT(StripComments("1#\n2 ##\n3#", {.comment_start = "##"}), EqualsText("1#\n2\n3#"));
  EXPECT_THAT(
      StripComments("1#\n2 ##\n3#", {.comment_start = "##", .strip_trailing_whitespace = false}),
      EqualsText("1#\n2 \n3#"));
  EXPECT_THAT(
      StripComments("1#\n'2 #' #'\n3#", {.comment_start = "#", .strip_trailing_whitespace = false}),
      EqualsText("1\n'2 \n3"));
  EXPECT_THAT(StripComments("1#\n'2 #' #'\n3#", {.comment_start = "#"}), EqualsText("1\n'2\n3"));
}

TEST_F(StripTest, Parsed) {
  EXPECT_THAT(StripParsedComments("", {.parse = {.stop_at_any_of = "#"}}), IsOkAndHolds(""));
  EXPECT_THAT(StripParsedComments("#", {.parse = {.stop_at_any_of = "#"}}), IsOkAndHolds(""));
  EXPECT_THAT(StripParsedComments("##", {.parse = {.stop_at_any_of = "#"}}), IsOkAndHolds(""));
  EXPECT_THAT(
      StripParsedComments("1#\n2 ##\n3#", {.parse = {.stop_at_any_of = "#"}}), IsOkAndHolds(EqualsText("1\n2\n3")));
  EXPECT_THAT(
      StripParsedComments("1#\n2 ##\n3#", {.parse = {.stop_at_any_of = "#"}, .strip_trailing_whitespace = false}),
      IsOkAndHolds(EqualsText("1\n2 \n3")));
  EXPECT_THAT(
      StripParsedComments(
          "1#\n'2 #' #\n3#",
          {.parse = {.stop_at_any_of = "#", .remove_quotes = false}, .strip_trailing_whitespace = false}),
      IsOkAndHolds(EqualsText("1\n'2 #' \n3")));
  EXPECT_THAT(
      StripParsedComments("1#\n'2 #' #\n3#", {.parse = {.stop_at_any_of = "#", .remove_quotes = false}}),
      IsOkAndHolds(EqualsText("1\n'2 #'\n3")));
}

TEST_F(StripTest, ParsedCommentsReportMalformedQuotedInput) {
  static constexpr StripParsedCommentArgs kArgs{.parse = {.stop_at_any_of = "#"}};
  EXPECT_THAT(
      StripParsedLineComments("'unterminated", kArgs),
      StatusIs(absl::StatusCode::kInvalidArgument, "Cannot parse input."));
  EXPECT_THAT(
      StripParsedComments("good\n'unterminated\nnot visited", kArgs),
      StatusIs(absl::StatusCode::kInvalidArgument, "Cannot parse input."));
}

}  // namespace
}  // namespace mbo::strings
