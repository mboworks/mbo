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

#include "mbo/types/string_or_view.h"

#include <compare>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "absl/hash/hash.h"
#include "absl/hash/hash_testing.h"
#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo {
namespace {

using ::testing::Eq;
using ::testing::Ge;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;

constexpr StringOrView kEmptyLiteral{""};
constexpr StringOrView kLiteral{"literal"};
static_assert(kEmptyLiteral.view().empty());
static_assert(kLiteral.view() == "literal");
static_assert(!kLiteral.owns_string());
static_assert(std::is_nothrow_move_constructible_v<StringOrView>);
static_assert(std::is_nothrow_move_assignable_v<StringOrView>);
static_assert(!std::is_constructible_v<StringOrView, const char*>);
static_assert(std::is_convertible_v<StringOrView, std::string_view>);
static_assert(std::is_same_v<StringOrView::iterator, StringOrView::const_iterator>);
static_assert(std::is_same_v<StringOrView::reference, StringOrView::const_reference>);
static_assert(StringOrView::npos == std::string_view::npos);

struct StringOrViewTest : ::testing::Test {};

TEST_F(StringOrViewTest, DefaultIsEmptyBorrowedView) {
  constexpr StringOrView value;
  EXPECT_THAT(value.view(), IsEmpty());
  EXPECT_THAT(value.owns_string(), IsFalse());
}

TEST_F(StringOrViewTest, BorrowsStringViewAndLiteral) {
  constexpr std::string_view kText = "borrowed";
  const StringOrView borrowed{kText};
  const StringOrView literal{"literal"};

  EXPECT_THAT(borrowed.view(), Eq(kText));
  EXPECT_THAT(&borrowed.view().front(), Eq(&kText.front()));
  EXPECT_THAT(borrowed.owns_string(), IsFalse());
  EXPECT_THAT(literal.view(), Eq("literal"));
  EXPECT_THAT(literal.view().size(), Eq(7));
  EXPECT_THAT(literal.owns_string(), IsFalse());
}

TEST_F(StringOrViewTest, OwnsCopiedLvalueAndMovedRvalue) {
  std::string source = "copied source";
  const StringOrView copied{source};
  source.assign("changed");

  std::string moved_source = "moved source";
  const StringOrView moved{std::move(moved_source)};

  EXPECT_THAT(copied.view(), Eq("copied source"));
  EXPECT_THAT(copied.owns_string(), IsTrue());
  EXPECT_THAT(moved.view(), Eq("moved source"));
  EXPECT_THAT(moved.owns_string(), IsTrue());
}

TEST_F(StringOrViewTest, CopyPreservesRepresentationAndLifetime) {
  constexpr std::string_view kBorrowedText = "borrowed";
  const StringOrView borrowed{kBorrowedText};
  const StringOrView borrowed_copy{borrowed};  // NOLINT(performance-unnecessary-copy-initialization)
  EXPECT_THAT(&borrowed_copy.view().front(), Eq(&borrowed.view().front()));
  EXPECT_THAT(borrowed_copy.owns_string(), IsFalse());

  const StringOrView owned{std::string(128, 'x')};
  StringOrView owned_copy{owned};
  EXPECT_THAT(&owned_copy.view().front(), Not(Eq(&owned.view().front())));
  owned_copy = StringOrView{"replacement"};
  EXPECT_THAT(owned.view(), Eq(std::string(128, 'x')));
  EXPECT_THAT(owned.owns_string(), IsTrue());
}

TEST_F(StringOrViewTest, CopyAndMoveAssignmentPreserveRepresentation) {
  constexpr std::string_view kBorrowedText = "borrowed";
  const StringOrView borrowed{kBorrowedText};
  StringOrView assigned{std::string{"owned"}};
  assigned = borrowed;
  EXPECT_THAT(&assigned.view().front(), Eq(&kBorrowedText.front()));
  EXPECT_THAT(assigned.owns_string(), IsFalse());

  const StringOrView owned{std::string{"owned"}};
  assigned = owned;
  EXPECT_THAT(assigned.view(), Eq("owned"));
  EXPECT_THAT(assigned.owns_string(), IsTrue());

  StringOrView moved;
  moved = std::move(assigned);
  EXPECT_THAT(moved.view(), Eq("owned"));
  EXPECT_THAT(moved.owns_string(), IsTrue());

  StringOrView moved_borrowed;
  moved_borrowed = StringOrView{kBorrowedText};
  EXPECT_THAT(&moved_borrowed.view().front(), Eq(&kBorrowedText.front()));
  EXPECT_THAT(moved_borrowed.owns_string(), IsFalse());
}

TEST_F(StringOrViewTest, DistinguishesOwnedAndBorrowedEmptyValues) {
  const StringOrView borrowed{std::string_view{}};
  const StringOrView owned{std::string{}};
  EXPECT_THAT(borrowed.view(), IsEmpty());
  EXPECT_THAT(borrowed.owns_string(), IsFalse());
  EXPECT_THAT(owned.view(), IsEmpty());
  EXPECT_THAT(owned.owns_string(), IsTrue());
  EXPECT_THAT(owned, Eq(borrowed));
}

TEST_F(StringOrViewTest, PreservesEmbeddedNulBytes) {
  constexpr StringOrView value{"a\0b"};
  EXPECT_THAT(value.view(), Eq(std::string_view{"a\0b", 3}));
}

TEST_F(StringOrViewTest, ComparesTextIndependentOfOwnership) {
  const StringOrView owned{std::string{"same"}};
  const StringOrView borrowed{std::string_view{"same"}};
  EXPECT_THAT(owned, Eq(borrowed));
  EXPECT_THAT(owned == std::string_view{"same"}, IsTrue());
  EXPECT_THAT(std::string_view{"same"} == owned, IsTrue());
  EXPECT_THAT(owned == std::string{"same"}, IsTrue());
  EXPECT_THAT(owned == "same", IsTrue());
  EXPECT_THAT(owned <=> borrowed, Eq(std::strong_ordering::equal));
  EXPECT_THAT(owned <=> "z", Eq(std::strong_ordering::less));
  EXPECT_THAT("a" <=> owned, Eq(std::strong_ordering::less));
}

TEST_F(StringOrViewTest, SupportsAbslStringify) {
  const StringOrView value{std::string{"formatted"}};
  EXPECT_THAT(absl::StrFormat("%v", value), Eq("formatted"));
}

TEST_F(StringOrViewTest, ProvidesReadOnlyElementAndIteratorAccess) {
  constexpr StringOrView value{"abcdef"};
  static_assert(value.size() == 6);
  static_assert(value.length() == 6);
  static_assert(!value.empty());
  static_assert(value[1] == 'b');  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  static_assert(value.at(2) == 'c');
  static_assert(value.front() == 'a');
  static_assert(value.back() == 'f');
  static_assert(value.data() == value.view().data());
  static_assert(*value.begin() == 'a');
  static_assert(*value.cbegin() == 'a');
  static_assert(*value.rbegin() == 'f');
  static_assert(*value.crbegin() == 'f');
  static_assert(value.end() - value.begin() == 6);
  static_assert(value.cend() - value.cbegin() == 6);
  static_assert(value.rend() - value.rbegin() == 6);
  static_assert(value.crend() - value.crbegin() == 6);
  const std::string_view converted = value;
  EXPECT_THAT(converted, Eq("abcdef"));
  EXPECT_THAT(value.size(), Eq(6));
  EXPECT_THAT(value.length(), Eq(6));
  EXPECT_THAT(value.empty(), IsFalse());
  EXPECT_THAT(value[1], Eq('b'));  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  EXPECT_THAT(value.at(2), Eq('c'));
  EXPECT_THAT(value.front(), Eq('a'));
  EXPECT_THAT(value.back(), Eq('f'));
  const char* const data = value.data();
  EXPECT_THAT(data, Eq(&value.view().front()));
  EXPECT_THAT(*value.begin(), Eq('a'));
  EXPECT_THAT(*value.cbegin(), Eq('a'));
  EXPECT_THAT(*value.rbegin(), Eq('f'));
  EXPECT_THAT(*value.crbegin(), Eq('f'));
  EXPECT_THAT(value.end() - value.begin(), Eq(6));
  EXPECT_THAT(value.cend() - value.cbegin(), Eq(6));
  EXPECT_THAT(value.rend() - value.rbegin(), Eq(6));
  EXPECT_THAT(value.crend() - value.crbegin(), Eq(6));
  EXPECT_THAT(value.max_size(), Ge(value.size()));
}

TEST_F(StringOrViewTest, CopiesAndReturnsSubstringsAsViews) {
  constexpr StringOrView value{"abcdef"};
  char copied[4]{};  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  EXPECT_THAT(value.copy(copied, 3, 2), Eq(3));
  EXPECT_THAT(std::string_view(copied, 3), Eq("cde"));
  EXPECT_THAT(value.substr(2, 3), Eq("cde"));
  EXPECT_THAT(value.substr(4), Eq("ef"));
  static_assert(value.subview(2, 3) == "cde");
  static_assert(value.subview(4) == "ef");
  EXPECT_THAT(value.subview(2, 3), Eq("cde"));
  EXPECT_THAT(value.subview(4), Eq("ef"));
}

TEST_F(StringOrViewTest, ProvidesStringViewComparisonOverloads) {
  constexpr StringOrView value{"abcdef"};
  EXPECT_THAT(value.compare(std::string_view{"abcdef"}), Eq(0));
  EXPECT_THAT(value.compare("abcdef"), Eq(0));
  EXPECT_THAT(value.compare(2, 3, std::string_view{"cde"}), Eq(0));
  EXPECT_THAT(value.compare(2, 3, std::string_view{"-cde-"}, 1, 3), Eq(0));
  EXPECT_THAT(value.compare(2, 3, "cde"), Eq(0));
  EXPECT_THAT(value.compare(2, 3, "cde-more", 3), Eq(0));
  EXPECT_THAT(value.compare("abcdee"), Not(Eq(0)));
}

TEST_F(StringOrViewTest, ConvertsOwnedValueToString) {
  const StringOrView value{std::string{"owned"}};
  EXPECT_THAT(static_cast<std::string>(value), Eq("owned"));
}

TEST_F(StringOrViewTest, ProvidesPrefixSuffixAndContainmentQueries) {
  constexpr StringOrView value{"abcdef"};
  static_assert(value.starts_with(std::string_view{"abc"}));
  static_assert(value.starts_with('a'));
  static_assert(value.starts_with("abc"));
  static_assert(value.ends_with(std::string_view{"def"}));
  static_assert(value.ends_with('f'));
  static_assert(value.ends_with("def"));
  static_assert(value.contains(std::string_view{"cd"}));
  static_assert(value.contains('c'));
  static_assert(value.contains("cd"));
  EXPECT_THAT(value.starts_with(std::string_view{"abc"}), IsTrue());
  EXPECT_THAT(value.starts_with('a'), IsTrue());
  EXPECT_THAT(value.starts_with("abc"), IsTrue());
  EXPECT_THAT(value.ends_with(std::string_view{"def"}), IsTrue());
  EXPECT_THAT(value.ends_with('f'), IsTrue());
  EXPECT_THAT(value.ends_with("def"), IsTrue());
  EXPECT_THAT(value.contains(std::string_view{"cd"}), IsTrue());
  EXPECT_THAT(value.contains('c'), IsTrue());
  EXPECT_THAT(value.contains("cd"), IsTrue());
}

TEST_F(StringOrViewTest, ProvidesStringViewSearchOperations) {
  constexpr StringOrView value{"abcaabbcc"};
  static_assert(value.find(std::string_view{"bc"}) == 1);
  static_assert(value.find('a', 1) == 3);
  static_assert(value.find("aabb", 0, 2) == 3);
  static_assert(value.find("bb") == 5);
  static_assert(value.rfind(std::string_view{"bc"}) == 6);
  static_assert(value.rfind('a') == 4);
  static_assert(value.find_first_of(std::string_view{"xyc"}) == 2);
  static_assert(value.find_last_of('a') == 4);
  static_assert(value.find_first_not_of(std::string_view{"ab"}) == 2);
  static_assert(value.find_last_not_of("bc") == 4);
  EXPECT_THAT(value.find(std::string_view{"bc"}), Eq(1));
  EXPECT_THAT(value.find('a', 1), Eq(3));
  EXPECT_THAT(value.find("aabb", 0, 2), Eq(3));
  EXPECT_THAT(value.find("bb"), Eq(5));
  EXPECT_THAT(value.rfind(std::string_view{"bc"}), Eq(6));
  EXPECT_THAT(value.rfind('a'), Eq(4));
  EXPECT_THAT(value.rfind("bc", StringOrView::npos, 2), Eq(6));
  EXPECT_THAT(value.rfind("bc"), Eq(6));
  EXPECT_THAT(value.find_first_of(std::string_view{"xyc"}), Eq(2));
  EXPECT_THAT(value.find_first_of('a', 1), Eq(3));
  EXPECT_THAT(value.find_first_of("xyc", 0, 3), Eq(2));
  EXPECT_THAT(value.find_first_of("xyc"), Eq(2));
  EXPECT_THAT(value.find_last_of(std::string_view{"ay"}), Eq(4));
  EXPECT_THAT(value.find_last_of('a'), Eq(4));
  EXPECT_THAT(value.find_last_of("ay", StringOrView::npos, 2), Eq(4));
  EXPECT_THAT(value.find_last_of("ay"), Eq(4));
  EXPECT_THAT(value.find_first_not_of(std::string_view{"ab"}), Eq(2));
  EXPECT_THAT(value.find_first_not_of('a'), Eq(1));
  EXPECT_THAT(value.find_first_not_of("ab", 0, 2), Eq(2));
  EXPECT_THAT(value.find_first_not_of("ab"), Eq(2));
  EXPECT_THAT(value.find_last_not_of(std::string_view{"bc"}), Eq(4));
  EXPECT_THAT(value.find_last_not_of('c'), Eq(6));
  EXPECT_THAT(value.find_last_not_of("bc", StringOrView::npos, 2), Eq(4));
  EXPECT_THAT(value.find_last_not_of("bc"), Eq(4));
}

TEST_F(StringOrViewTest, SupportsStreamAndStandardFormattingWithoutLosingNulBytes) {
  constexpr StringOrView value{"a\0b"};
  std::ostringstream out;
  out << value;
  EXPECT_THAT(out.str(), Eq(std::string{"a\0b", 3}));
#if defined(__cpp_lib_format) && __cpp_lib_format >= (20L * 100 + 19) * 100 + 7
  EXPECT_THAT(std::format("{}", value), Eq(std::string{"a\0b", 3}));
#endif
}

TEST_F(StringOrViewTest, HashesRepresentedTextIndependentOfOwnership) {
  const StringOrView borrowed{"same"};
  const StringOrView owned{std::string{"same"}};
  const StringOrView other{"other"};
  EXPECT_THAT(absl::VerifyTypeImplementsAbslHashCorrectly({borrowed, owned, other}), IsTrue());
  EXPECT_THAT(absl::HashOf(borrowed), Eq(absl::HashOf(owned)));
  EXPECT_THAT(std::hash<StringOrView>{}(borrowed), Eq(std::hash<StringOrView>{}(owned)));
  EXPECT_THAT(std::hash<StringOrView>{}(borrowed), Eq(std::hash<std::string_view>{}(borrowed.view())));
}

}  // namespace
}  // namespace mbo
