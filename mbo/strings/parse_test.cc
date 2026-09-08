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

#include "mbo/strings/parse.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/status.h"

namespace mbo::strings {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pair;

class ParseTest : public ::testing::Test {
 public:
  static absl::StatusOr<std::pair<std::string, std::string>> ParseString(
      const ParseOptions& options,
      std::string_view data) {
    MBO_ASSIGN_OR_RETURN(std::string result, mbo::strings::ParseString(options, data));
    return std::make_pair(std::move(result), std::string(data));
  }

  static absl::StatusOr<std::pair<std::vector<std::string>, std::string>> ParseStringList(
      const ParseOptions& options,
      std::string_view data) {
    MBO_ASSIGN_OR_RETURN(std::vector<std::string> result, mbo::strings::ParseStringList(options, data));
    return std::make_pair(std::move(result), std::string(data));
  }
};

TEST_F(ParseTest, ParseString) {
  EXPECT_THAT(ParseString({}, ""), IsOkAndHolds(Pair("", "")));
  EXPECT_THAT(ParseString({}, "42"), IsOkAndHolds(Pair("42", "")));
  EXPECT_THAT(ParseString({}, "4,2"), IsOkAndHolds(Pair("4,2", "")));
}

TEST_F(ParseTest, ParseStringOctal) {
  EXPECT_THAT(ParseString({}, "\\1234"), IsOkAndHolds(Pair("S4", "")));
  EXPECT_THAT(ParseString({}, "\\19"), IsOkAndHolds(Pair("\19", "")));
  EXPECT_THAT(
      ParseString({}, "\\9a"),
      StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input has unsupported escape sequence."));
  EXPECT_THAT(ParseString({}, "\\o{123}9"), IsOkAndHolds(Pair("S9", "")));
  EXPECT_THAT(ParseString({}, "4,2"), IsOkAndHolds(Pair("4,2", "")));
}

TEST_F(ParseTest, ParseStringHex) {
  EXPECT_THAT(ParseString({}, ""), IsOkAndHolds(Pair("", "")));
  EXPECT_THAT(ParseString({}, "\\x0"), IsOkAndHolds(Pair(std::string(1, '\0'), "")));
  EXPECT_THAT(ParseString({}, "\\x4"), IsOkAndHolds(Pair("\4", "")));
  EXPECT_THAT(ParseString({}, "\\x42"), IsOkAndHolds(Pair("\x42", "")));
  EXPECT_THAT(ParseString({}, "\\xaf"), IsOkAndHolds(Pair(std::string(1, static_cast<char>(0xAF)), "")));
  EXPECT_THAT(ParseString({}, "\\xAF"), IsOkAndHolds(Pair(std::string(1, static_cast<char>(0xAF)), "")));
  EXPECT_THAT(ParseString({}, "\\xaF"), IsOkAndHolds(Pair(std::string(1, static_cast<char>(0xAF)), "")));
  EXPECT_THAT(ParseString({}, "\\xff"), IsOkAndHolds(Pair(std::string(1, static_cast<char>(0xFF)), "")));
  EXPECT_THAT(ParseString({}, "\\x423"), IsOkAndHolds(Pair(absl::StrCat("\x42", "3"), "")));
  EXPECT_THAT(ParseString({}, "\\x{42}"), IsOkAndHolds(Pair("\x42", "")));
  EXPECT_THAT(ParseString({}, "\\x{4}"), IsOkAndHolds(Pair("\4", "")));
  EXPECT_THAT(ParseString({}, "\\x{aF}"), IsOkAndHolds(Pair(std::string(1, static_cast<char>(0xAF)), "")));
  EXPECT_THAT(
      ParseString({}, "\\x{423}"),
      StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input has bad braced hex sequence."));
  EXPECT_THAT(
      ParseString({}, "\\x{100}"),
      StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input has bad braced hex sequence."));
}

TEST_F(ParseTest, ParseStringSimpleAndCustomEscapes) {
  EXPECT_THAT(
      ParseString({}, R"(\?\'\"\\\a\b\f\n\r\t\v)"), IsOkAndHolds(Pair(std::string_view("?'\"\\\a\b\f\n\r\t\v"), "")));
  static constexpr ParseOptions kCustom{.custom_escapes = "@"};
  EXPECT_THAT(ParseString(kCustom, R"(\@)"), IsOkAndHolds(Pair("@", "")));
}

TEST_F(ParseTest, ParseStringStopAtAnyOf) {
  ASSERT_THAT(ParseOptions().stop_at_any_of, IsEmpty());
  ASSERT_THAT(ParseOptions().split_at_any_of, IsEmpty());
  static constexpr ParseOptions kOpts{.stop_at_any_of = ".,"};
  EXPECT_THAT(ParseString(kOpts, ""), IsOkAndHolds(Pair("", "")));
  EXPECT_THAT(ParseString(kOpts, "."), IsOkAndHolds(Pair("", ".")));
  EXPECT_THAT(ParseString(kOpts, ","), IsOkAndHolds(Pair("", ",")));
  EXPECT_THAT(ParseString(kOpts, "4;2"), IsOkAndHolds(Pair("4;2", "")));
  EXPECT_THAT(ParseString(kOpts, "4,2"), IsOkAndHolds(Pair("4", ",2")));
  EXPECT_THAT(ParseString(kOpts, "4.2"), IsOkAndHolds(Pair("4", ".2")));
  EXPECT_THAT(ParseString(kOpts, "."), IsOkAndHolds(Pair("", ".")));
}

TEST_F(ParseTest, ParseStringStopAtStr) {
  ASSERT_THAT(ParseOptions().stop_at_any_of, IsEmpty());
  ASSERT_THAT(ParseOptions().stop_at_str, IsEmpty());
  ASSERT_THAT(ParseOptions().split_at_any_of, IsEmpty());
  static constexpr ParseOptions kOpts{.stop_at_str = "//", .remove_quotes = false};
  EXPECT_THAT(ParseString(kOpts, ""), IsOkAndHolds(Pair("", "")));
  EXPECT_THAT(ParseString(kOpts, "//"), IsOkAndHolds(Pair("", "//")));
  EXPECT_THAT(ParseString(kOpts, "x //"), IsOkAndHolds(Pair("x ", "//")));
  EXPECT_THAT(ParseString(kOpts, "x('//'); // y"), IsOkAndHolds(Pair("x('//'); ", "// y")));
}

TEST_F(ParseTest, ParseStringSplitAtAnyOf) {
  ASSERT_THAT(ParseOptions().stop_at_any_of, IsEmpty());
  ASSERT_THAT(ParseOptions().stop_at_any_of, IsEmpty());
  ASSERT_THAT(ParseOptions().split_at_any_of, IsEmpty());
  static constexpr ParseOptions kOpts{.split_at_any_of = ".,"};
  EXPECT_THAT(ParseString(kOpts, ""), IsOkAndHolds(Pair("", "")));
  EXPECT_THAT(ParseString(kOpts, "."), IsOkAndHolds(Pair("", ".")));
  EXPECT_THAT(ParseString(kOpts, ","), IsOkAndHolds(Pair("", ",")));
  EXPECT_THAT(ParseString(kOpts, "4;2"), IsOkAndHolds(Pair("4;2", "")));
  EXPECT_THAT(ParseString(kOpts, "4,2"), IsOkAndHolds(Pair("4", ",2")));
  EXPECT_THAT(ParseString(kOpts, "4.2"), IsOkAndHolds(Pair("4", ".2")));
  EXPECT_THAT(ParseString(kOpts, "."), IsOkAndHolds(Pair("", ".")));
}

TEST_F(ParseTest, ParseStringRemoveQuotes) {
  ASSERT_THAT(ParseOptions().remove_quotes, true);
  ASSERT_THAT(ParseOptions().allow_unquoted, true);
  EXPECT_THAT(ParseString({}, ""), IsOkAndHolds(Pair("", "")));
  EXPECT_THAT(ParseString({}, "."), IsOkAndHolds(Pair(".", "")));
  EXPECT_THAT(ParseString({}, "'1,2'\",3\""), IsOkAndHolds(Pair("1,2,3", "")));
  EXPECT_THAT(ParseString({}, "'1,2',3"), IsOkAndHolds(Pair("1,2,3", "")));
  static constexpr ParseOptions kOpts{
      .remove_quotes = false,
  };
  EXPECT_THAT(ParseString(kOpts, ""), IsOkAndHolds(Pair("", "")));
  EXPECT_THAT(ParseString(kOpts, "."), IsOkAndHolds(Pair(".", "")));
  EXPECT_THAT(ParseString(kOpts, "'1,2'\",3\""), IsOkAndHolds(Pair("'1,2'\",3\"", "")));
  EXPECT_THAT(ParseString(kOpts, "'1,2',3"), IsOkAndHolds(Pair("'1,2',3", "")));
}

TEST_F(ParseTest, ParseStringHonorsEnabledQuoteTypesIndependently) {
  static constexpr ParseOptions kBothEnabled{.split_at_any_of = ","};
  EXPECT_THAT(ParseString(kBothEnabled, R"("a,b")"), IsOkAndHolds(Pair("a,b", "")));
  EXPECT_THAT(ParseString(kBothEnabled, "'a,b'"), IsOkAndHolds(Pair("a,b", "")));
  EXPECT_THAT(ParseString(kBothEnabled, R"("a'b")"), IsOkAndHolds(Pair("a'b", "")));
  EXPECT_THAT(ParseString(kBothEnabled, R"('a"b')"), IsOkAndHolds(Pair("a\"b", "")));

  static constexpr ParseOptions kDoubleOnly{
      .split_at_any_of = ",",
      .enable_single_quotes = false,
  };
  EXPECT_THAT(ParseString(kDoubleOnly, R"("a,b")"), IsOkAndHolds(Pair("a,b", "")));
  EXPECT_THAT(ParseString(kDoubleOnly, "'a,b'"), IsOkAndHolds(Pair("'a", ",b'")));

  static constexpr ParseOptions kSingleOnly{
      .split_at_any_of = ",",
      .enable_double_quotes = false,
  };
  EXPECT_THAT(ParseString(kSingleOnly, R"("a,b")"), IsOkAndHolds(Pair("\"a", ",b\"")));
  EXPECT_THAT(ParseString(kSingleOnly, "'a,b'"), IsOkAndHolds(Pair("a,b", "")));

  static constexpr ParseOptions kNeitherEnabled{
      .split_at_any_of = ",",
      .enable_double_quotes = false,
      .enable_single_quotes = false,
  };
  EXPECT_THAT(ParseString(kNeitherEnabled, R"("a,b")"), IsOkAndHolds(Pair("\"a", ",b\"")));
  EXPECT_THAT(ParseString(kNeitherEnabled, "'a,b'"), IsOkAndHolds(Pair("'a", ",b'")));
}

TEST_F(ParseTest, ParseStringAllowUnquoted) {
  ASSERT_THAT(ParseOptions().remove_quotes, true);
  ASSERT_THAT(ParseOptions().allow_unquoted, true);
  EXPECT_THAT(ParseString({}, ""), IsOkAndHolds(Pair("", "")));
  EXPECT_THAT(ParseString({}, "."), IsOkAndHolds(Pair(".", "")));
  EXPECT_THAT(ParseString({}, "'1,2'\",3\""), IsOkAndHolds(Pair("1,2,3", "")));
  EXPECT_THAT(ParseString({}, "'1,2',3"), IsOkAndHolds(Pair("1,2,3", "")));
  static constexpr ParseOptions kOpts{
      .allow_unquoted = false,
  };
  EXPECT_THAT(ParseString(kOpts, ""), IsOkAndHolds(Pair("", "")));
  EXPECT_THAT(ParseString(kOpts, "."), IsOkAndHolds(Pair("", ".")));
  EXPECT_THAT(ParseString(kOpts, "'1,2'\",3\""), IsOkAndHolds(Pair("1,2,3", "")));
  EXPECT_THAT(ParseString(kOpts, "'1,2',3"), IsOkAndHolds(Pair("1,2", ",3")));
}

TEST_F(ParseTest, ParseStringErrors) {
  EXPECT_THAT(ParseString({}, "\\"), StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input ends in '\\'."));
  EXPECT_THAT(
      ParseString({}, "\\o"),
      StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input has bad octal sequence."));
  EXPECT_THAT(
      ParseString({}, "\\o{"),
      StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input has bad braced octal sequence."));
  EXPECT_THAT(
      ParseString({}, "\\o{}"),
      StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input has bad braced octal sequence."));
  EXPECT_THAT(
      ParseString({}, "\\o{12"),
      StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input has bad braced octal sequence."));
  EXPECT_THAT(
      ParseString({}, "\\x"), StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input has bad hex sequence."));
  EXPECT_THAT(
      ParseString({}, "\\xz"), StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input has bad hex sequence."));
  EXPECT_THAT(
      ParseString({}, "\\x{"),
      StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input has bad braced hex sequence."));
  EXPECT_THAT(
      ParseString({}, "\\x{}"),
      StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input has bad braced hex sequence."));
  EXPECT_THAT(
      ParseString({}, "'"),
      StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input has unterminated single quotes (')."));
  EXPECT_THAT(
      ParseString({}, "\""),
      StatusIs(absl::StatusCode::kInvalidArgument, "ParseString input has unterminated double quotes (\")."));
  EXPECT_THAT(
      ParseString({}, "\\u"),
      StatusIs(absl::StatusCode::kUnimplemented, "ParseString input has not yet supported unicode escape sequence."));
  EXPECT_THAT(
      ParseString({}, "\\U"),
      StatusIs(absl::StatusCode::kUnimplemented, "ParseString input has not yet supported unicode escape sequence."));
  EXPECT_THAT(
      ParseString({}, "\\N"), StatusIs(
                                  absl::StatusCode::kUnimplemented,
                                  "ParseString input has not yet supported named unicode char escape sequence."));
}

TEST_F(ParseTest, ParseStringList) {
  EXPECT_THAT(ParseStringList({}, ""), IsOkAndHolds(Pair(IsEmpty(), "")));
  EXPECT_THAT(ParseStringList({}, "1,2"), IsOkAndHolds(Pair(ElementsAre("1", "2"), "")));
}

TEST_F(ParseTest, ParseStringListSplitAtAnyOf) {
  EXPECT_THAT(ParseStringList({.split_at_any_of = "."}, ""), IsOkAndHolds(Pair(IsEmpty(), "")));
  EXPECT_THAT(ParseStringList({.split_at_any_of = "."}, "."), IsOkAndHolds(Pair(ElementsAre("", ""), "")));
  EXPECT_THAT(ParseStringList({.split_at_any_of = "."}, ".4,2"), IsOkAndHolds(Pair(ElementsAre("", "4,2"), "")));
  EXPECT_THAT(ParseStringList({.split_at_any_of = "."}, "4,2."), IsOkAndHolds(Pair(ElementsAre("4,2", ""), "")));
  EXPECT_THAT(ParseStringList({.split_at_any_of = "."}, "4,2"), IsOkAndHolds(Pair(ElementsAre("4,2"), "")));
  EXPECT_THAT(ParseStringList({.split_at_any_of = ","}, "4,2"), IsOkAndHolds(Pair(ElementsAre("4", "2"), "")));
  EXPECT_THAT(
      ParseStringList({.split_at_any_of = ".,;"}, "4,3;2.1"), IsOkAndHolds(Pair(ElementsAre("4", "3", "2", "1"), "")));
  EXPECT_THAT(
      ParseStringList({.split_at_any_of = ".,;"}, ".,;."), IsOkAndHolds(Pair(ElementsAre("", "", "", "", ""), "")));
}

TEST_F(ParseTest, ParseStringListStopAtAnyOf) {
  static constexpr ParseOptions kOpts{
      .stop_at_any_of = ".",
  };
  EXPECT_THAT(ParseStringList(kOpts, ""), IsOkAndHolds(Pair(IsEmpty(), "")));
  EXPECT_THAT(ParseStringList(kOpts, "."), IsOkAndHolds(Pair(IsEmpty(), ".")));
  EXPECT_THAT(ParseStringList(kOpts, "1,2.3,4"), IsOkAndHolds(Pair(ElementsAre("1", "2"), ".3,4")));
  EXPECT_THAT(ParseStringList(kOpts, "1,2,.3,4"), IsOkAndHolds(Pair(ElementsAre("1", "2", ""), ".3,4")));
}

}  // namespace
}  // namespace mbo::strings
