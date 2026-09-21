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

#include "mbo/testing/matchers.h"

#include <cstddef>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::testing {
namespace {

// NOLINTBEGIN(*-magic-numbers)

using ::testing::ElementsAre;
using ::testing::Ge;
using ::testing::IsEmpty;
using ::testing::Key;
using ::testing::Not;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;
using ::testing::WhenSorted;

class MatcherTest : public ::testing::Test {
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
    const bool result = ::testing::Matcher<V>(matcher).MatchAndExplain(value, &os);
    return {result, os.str()};
  }
};

TEST_F(MatcherTest, CapacityIs) {
  EXPECT_THAT(std::vector<int>{}, CapacityIs(Ge(0)));
  EXPECT_THAT((std::vector<int>{1, 2}), CapacityIs(Ge(2)));
  std::vector<int> vector;
  vector.shrink_to_fit();
  const std::size_t before = vector.capacity();
  EXPECT_THAT(vector, CapacityIs(before));
  vector.reserve((1 + before) * 2);
  const std::size_t after = vector.capacity();
  ASSERT_THAT(before, Not(after));
  EXPECT_THAT(vector, CapacityIs(after));
}

TEST_F(MatcherTest, CapacityIsDescriptions) {
  {
    const ::testing::Matcher<std::vector<int>> matcher = CapacityIs(1);
    EXPECT_THAT(Describe(matcher), "has a capacity that is equal to 1");
    EXPECT_THAT(DescribeNegation(matcher), "has a capacity that isn't equal to 1");
    EXPECT_THAT(MatchAndExplain(matcher, std::vector<int>(1)), Pair(true, "whose capacity 1 matches"));
    EXPECT_THAT(MatchAndExplain(matcher, std::vector<int>(2, 2)), Pair(false, "whose capacity 2 doesn't match"));
  }
  {
    const ::testing::Matcher<std::vector<int>> matcher = CapacityIs(Ge(2));
    EXPECT_THAT(Describe(matcher), "has a capacity that is >= 2");
    EXPECT_THAT(DescribeNegation(matcher), "has a capacity that isn't >= 2");
  }
}

TEST_F(MatcherTest, WhenTransformedBySameType) {
  {
    // std::vector: int -> int
    const std::vector<int> set{1, 2};
    EXPECT_THAT(set, WhenTransformedBy([](int v) { return v + 2; }, ElementsAre(3, 4)));
  }
  {
    // std::set: int -> int
    const std::set<int> set{1, 2};
    EXPECT_THAT(set, WhenTransformedBy([](int v) { return v + 2; }, ElementsAre(3, 4)));
  }
  {
    // std::map: (const int, int) -> (const int, int)
    const std::map<int, int> map{{1, 2}, {3, 4}};
    EXPECT_THAT(
        map, WhenTransformedBy(
                 [](const std::pair<int, int>& v) { return std::make_pair(v.second, v.first); },
                 ElementsAre(Pair(2, 1), Pair(4, 3))));
  }
}

TEST_F(MatcherTest, WhenTransformedByConversion) {
  {
    // std::vector: int -> std::string
    const std::vector<int> set{1, 2};
    EXPECT_THAT(set, WhenTransformedBy([](int v) { return absl::StrCat(v + 2); }, ElementsAre("3", "4")));
  }
  {
    // std::set: int -> std::string
    const std::set<int> set{1, 2};
    EXPECT_THAT(set, WhenTransformedBy([](int v) { return absl::StrCat(v + 2); }, ElementsAre("3", "4")));
  }
  {
    // std::vector: int -> (int, int)
    const std::vector<int> set{1, 2};
    EXPECT_THAT(
        set, WhenTransformedBy([](int v) { return std::make_pair(v, v + 2); }, ElementsAre(Pair(1, 3), Pair(2, 4))));
  }
  {
    // std::map: (int, int) -> (std::string, std::string)
    const std::map<int, int> map{{1, 2}, {3, 4}};
    EXPECT_THAT(
        map,
        WhenTransformedBy(
            [](const std::pair<int, int>& v) { return std::make_pair(absl::StrCat(v.second), absl::StrCat(v.first)); },
            ElementsAre(Pair("2", "1"), Pair("4", "3"))));
  }
  {
    // std::map(int, int) -> (std::string, std::string)
    // Only comparing Keys
    const std::map<int, int> map{{1, 2}, {3, 4}};
    EXPECT_THAT(
        map,
        WhenTransformedBy(
            [](const std::pair<int, int>& v) { return std::make_pair(absl::StrCat(v.second), absl::StrCat(v.first)); },
            ElementsAre(Key("2"), Key("4"))));
    // std::map(int, int) -> key=int
    EXPECT_THAT(map, WhenTransformedBy([](const std::pair<int, int>& v) { return v.first; }, ElementsAre(1, 3)));
  }
  {
    // std::map(int, int) -> std::string
    const std::map<int, int> map{{1, 2}, {3, 4}};
    EXPECT_THAT(
        map, WhenTransformedBy(
                 [](const std::pair<int, int>& v) { return absl::StrCat(v.first + v.second); }, ElementsAre("3", "7")));
  }
  {
    // `WhenSorted` and `Unordered*`
    const std::vector<int> vector{4, 1, 2, 3, 0};
    EXPECT_THAT(vector, WhenTransformedBy([](int v) { return v; }, UnorderedElementsAre(0, 1, 2, 3, 4)));
    EXPECT_THAT(vector, WhenTransformedBy([](int v) { return v; }, WhenSorted(ElementsAre(0, 1, 2, 3, 4))));
  }
  {
    const std::map<std::string, std::string> map{{"1", "2"}, {"3", "4"}};
    EXPECT_THAT(  // std::map: (std::string, std::string) -> (int, int)
        map, WhenTransformedBy(
                 [](const std::pair<std::string, std::string>& v) {
                   std::pair<int, int> result;
                   (void)absl::SimpleAtoi(v.first, &result.second);
                   (void)absl::SimpleAtoi(v.second, &result.first);
                   return result;
                 },
                 UnorderedElementsAre(Pair(2, 1), Pair(4, 3))));
    EXPECT_THAT(  // std::map: (std::string, std::string) -> Modified copy(std::string, std::string)
        map, WhenTransformedBy(
                 [](std::pair<std::string, std::string> v) {
                   std::swap(v.first, v.second);
                   return v;
                 },
                 UnorderedElementsAre(Pair("2", "1"), Pair("4", "3"))));
  }
}

TEST_F(MatcherTest, WhenTransformedByMoveOnly) {
  {
    std::set<std::unique_ptr<std::string>> set;
    set.emplace(std::make_unique<std::string>("foo"));
    set.emplace(std::make_unique<std::string>("bar"));
    const std::vector<std::string_view> vector{"bar", "foo"};
    EXPECT_THAT(  // std::set: std::unique_ptr<std::string> -> std::string == std::string_view
        set, WhenTransformedBy(
                 [](const std::unique_ptr<std::string>& str) { return *str; }, UnorderedElementsAreArray(vector)));
    // Reverse is not possible as no container matcher will take a reference to a container of move-only values.
    // In fact all container matchers are copying the elements.
  }
}

TEST_F(MatcherTest, WhenTransformedByDescriptions) {
  {
    const ::testing::Matcher<std::vector<int>> matcher = WhenTransformedBy([](int) { return 0; }, IsEmpty());
    EXPECT_THAT(Describe(matcher), "when transformed is empty");
    EXPECT_THAT(DescribeNegation(matcher), "when transformed isn't empty");
    EXPECT_THAT(MatchAndExplain(matcher, std::vector<int>()), Pair(true, "which (when transformed) matches"));
    EXPECT_THAT(
        MatchAndExplain(matcher, std::vector<int>(1)),
        Pair(false, "which (when transformed) doesn't match, whose size is 1"));
    EXPECT_THAT(
        MatchAndExplain(matcher, std::vector<int>(2, 2)),
        Pair(false, "which (when transformed) doesn't match, whose size is 2"));
  }
  {
    const ::testing::Matcher<std::vector<int>> matcher = WhenTransformedBy([](int) { return 0; }, ElementsAre(0));
    EXPECT_THAT(Describe(matcher), "when transformed has 1 element that is equal to 0");
    EXPECT_THAT(
        DescribeNegation(matcher),
        EqualsText("when transformed doesn't have 1 element, or\nelement #0 isn't equal to 0"));
    EXPECT_THAT(MatchAndExplain(matcher, std::vector<int>()), Pair(false, "which (when transformed) doesn't match"));
    EXPECT_THAT(MatchAndExplain(matcher, std::vector<int>(1)), Pair(true, "which (when transformed) matches"));
    EXPECT_THAT(
        MatchAndExplain(matcher, std::vector<int>(2, 2)),
        Pair(false, "which (when transformed) doesn't match, which has 2 elements"));
  }
  {
    const ::testing::Matcher<std::vector<int>> matcher = WhenTransformedBy([](int v) { return v; }, ElementsAre(0));
    EXPECT_THAT(
        MatchAndExplain(matcher, std::vector<int>{0, 1, 2, 3, 4}),
        Pair(false, "which (when transformed) doesn't match, which has 5 elements"));
  }
}

TEST_F(MatcherTest, IsElementOfContainer) {
  const std::vector<int> allowed = {1, 2, 3};
  EXPECT_THAT(2, IsElementOf(allowed));
  EXPECT_THAT(0, Not(IsElementOf(allowed)));
}

TEST_F(MatcherTest, IsElementOfRvalueContainer) {
  // Container taken by value, so a temporary survives long enough to match.
  EXPECT_THAT(2, IsElementOf(std::vector<int>{1, 2, 3}));
  EXPECT_THAT(0, Not(IsElementOf(std::vector<int>{1, 2, 3})));
}

TEST_F(MatcherTest, IsElementOfInitializerList) {
  EXPECT_THAT(2, IsElementOf({1, 2, 3}));
  EXPECT_THAT(0, Not(IsElementOf({1, 2, 3})));
}

TEST_F(MatcherTest, IsElementOfSet) {
  const std::set<std::string> allowed = {"a", "b", "c"};
  EXPECT_THAT("b", IsElementOf(allowed));
  EXPECT_THAT("z", Not(IsElementOf(allowed)));
}

TEST_F(MatcherTest, IsElementOfVectorOfStrings) {
  // String-literal subject (const char[]) against vector<string> elements:
  // the literal must compile and match without any caller-side conversion.
  const std::vector<std::string> allowed = {"alpha", "beta", "gamma"};
  EXPECT_THAT("beta", IsElementOf(allowed));
  EXPECT_THAT("zeta", Not(IsElementOf(allowed)));
}

TEST_F(MatcherTest, IsElementOfListOfStringPairs) {
  // Pair-of-string-literals subject (pair<const char*, const char*>) against
  // a list<pair<string, string>>: pair::operator== compares componentwise via
  // string == const char*, so no caller-side conversion is needed for either
  // half of the pair.
  const std::list<std::pair<std::string, std::string>> allowed = {
      {"a", "1"},
      {"b", "2"},
      {"c", "3"},
  };
  EXPECT_THAT(std::make_pair("b", "2"), IsElementOf(allowed));
  EXPECT_THAT(std::make_pair("b", "X"), Not(IsElementOf(allowed)));
  EXPECT_THAT(std::make_pair("z", "9"), Not(IsElementOf(allowed)));
}

TEST_F(MatcherTest, IsElementOfEmpty) {
  // An empty allowed-set never matches anything (mirrors AnyOfArray of empty).
  EXPECT_THAT(0, Not(IsElementOf(std::vector<int>{})));
}

TEST_F(MatcherTest, IsElementOfDescriptions) {
  const ::testing::Matcher<int> matcher = IsElementOf(std::vector<int>{1, 2, 3});
  EXPECT_THAT(Describe(matcher), "is element of {1, 2, 3}");
  EXPECT_THAT(DescribeNegation(matcher), "is not element of {1, 2, 3}");
  EXPECT_THAT(MatchAndExplain(matcher, 2), Pair(true, "which equals element #1 (2)"));
  EXPECT_THAT(MatchAndExplain(matcher, 99), Pair(false, ""));
}

TEST_F(MatcherTest, IsElementOfEmptyDescriptions) {
  const ::testing::Matcher<int> matcher = IsElementOf(std::vector<int>{});
  EXPECT_THAT(Describe(matcher), "is element of {}");
  EXPECT_THAT(DescribeNegation(matcher), "is not element of {}");
  EXPECT_THAT(MatchAndExplain(matcher, 0), Pair(false, ""));
}

TEST_F(MatcherTest, IsKeyAndIsValueOfMap) {
  const std::map<int, std::string> map{{1, "a"}, {2, "b"}};
  EXPECT_THAT(1, IsKeyOf(map));
  EXPECT_THAT(2, IsKeyOf(map));
  EXPECT_THAT(3, Not(IsKeyOf(map)));
  EXPECT_THAT("a", IsValueOf(map));
  EXPECT_THAT("b", IsValueOf(map));
  EXPECT_THAT("z", Not(IsValueOf(map)));
}

TEST_F(MatcherTest, IsKeyAndIsValueOfEmptyMap) {
  const std::map<int, std::string> map;
  EXPECT_THAT(0, Not(IsKeyOf(map)));
  EXPECT_THAT(42, Not(IsKeyOf(map)));
  EXPECT_THAT("", Not(IsValueOf(map)));
  EXPECT_THAT("anything", Not(IsValueOf(map)));
}

TEST_F(MatcherTest, IsKeyAndIsValueOfUnorderedMap) {
  const std::unordered_map<int, std::string> map{{1, "a"}, {2, "b"}};
  EXPECT_THAT(1, IsKeyOf(map));
  EXPECT_THAT(2, IsKeyOf(map));
  EXPECT_THAT(3, Not(IsKeyOf(map)));
  EXPECT_THAT("a", IsValueOf(map));
  EXPECT_THAT("b", IsValueOf(map));
  EXPECT_THAT("z", Not(IsValueOf(map)));
}

TEST_F(MatcherTest, IsKeyAndIsValueOfMultimap) {
  // multimap allows duplicate keys; from the membership orientation duplicate
  // entries are simply both present.
  const std::multimap<int, std::string> multi_map{{1, "a"}, {1, "b"}, {2, "c"}};
  EXPECT_THAT(1, IsKeyOf(multi_map));
  EXPECT_THAT(2, IsKeyOf(multi_map));
  EXPECT_THAT(3, Not(IsKeyOf(multi_map)));
  EXPECT_THAT("a", IsValueOf(multi_map));
  EXPECT_THAT("b", IsValueOf(multi_map));
  EXPECT_THAT("c", IsValueOf(multi_map));
  EXPECT_THAT("z", Not(IsValueOf(multi_map)));
}

TEST_F(MatcherTest, IsKeyOfStringMap) {
  const std::map<std::string, int> ordered{{"a", 1}, {"b", 2}};
  const std::unordered_map<std::string, int> unordered{{"a", 1}, {"b", 2}};
  EXPECT_THAT("a", IsKeyOf(ordered));
  EXPECT_THAT("a", IsKeyOf(unordered));
  EXPECT_THAT(std::string_view{"a"}, IsKeyOf(ordered));
  EXPECT_THAT(std::string_view{"a"}, IsKeyOf(unordered));
  EXPECT_THAT("z", Not(IsKeyOf(ordered)));
  EXPECT_THAT("z", Not(IsKeyOf(unordered)));
}

TEST_F(MatcherTest, IsKeyOfDescriptions) {
  const std::map<int, std::string> map{{1, "a"}, {2, "b"}};
  const ::testing::Matcher<int> matcher = IsKeyOf(map);
  EXPECT_THAT(Describe(matcher), "is a key of {1, 2}");
  EXPECT_THAT(DescribeNegation(matcher), "is not a key of {1, 2}");
  EXPECT_THAT(MatchAndExplain(matcher, 1), Pair(true, "which equals element #0 (1)"));
  EXPECT_THAT(MatchAndExplain(matcher, 99), Pair(false, ""));
}

TEST_F(MatcherTest, IsKeyOfEmptyMapDescriptions) {
  const std::map<int, std::string> map;
  const ::testing::Matcher<int> matcher = IsKeyOf(map);
  EXPECT_THAT(Describe(matcher), "is a key of {}");
  EXPECT_THAT(DescribeNegation(matcher), "is not a key of {}");
  EXPECT_THAT(MatchAndExplain(matcher, 42), Pair(false, ""));
}

TEST_F(MatcherTest, IsValueOfDescriptions) {
  const std::map<int, std::string> map{{1, "a"}, {2, "b"}};
  const ::testing::Matcher<std::string> matcher = IsValueOf(map);
  EXPECT_THAT(Describe(matcher), "is a value of {\"a\", \"b\"}");
  EXPECT_THAT(DescribeNegation(matcher), "is not a value of {\"a\", \"b\"}");
  EXPECT_THAT(MatchAndExplain(matcher, std::string{"a"}), Pair(true, "which equals element #0 (\"a\")"));
  EXPECT_THAT(MatchAndExplain(matcher, std::string{"z"}), Pair(false, ""));
}

TEST_F(MatcherTest, IsValueOfEmptyMapDescriptions) {
  const std::map<int, std::string> map;
  const ::testing::Matcher<std::string> matcher = IsValueOf(map);
  EXPECT_THAT(Describe(matcher), "is a value of {}");
  EXPECT_THAT(DescribeNegation(matcher), "is not a value of {}");
  EXPECT_THAT(MatchAndExplain(matcher, std::string{"anything"}), Pair(false, ""));
}

TEST_F(MatcherTest, IsElementOfHeterogeneousComparable) {
  // value_type of the container can differ from the subject's type as long as
  // gmock's Eq can compare them. The left side does not need to be converted
  // to the container's value_type by the caller.
  const std::vector<std::string> allowed = {"alpha", "beta"};
  EXPECT_THAT("alpha", IsElementOf(allowed));                    // const char[]
  EXPECT_THAT(std::string_view{"alpha"}, IsElementOf(allowed));  // string_view
  EXPECT_THAT(std::string{"alpha"}, IsElementOf(allowed));       // string
  EXPECT_THAT(std::string_view{"gamma"}, Not(IsElementOf(allowed)));
}

TEST_F(MatcherTest, EqualsText) {
  const ::testing::Matcher<std::string_view> matcher = EqualsText("abc");
  EXPECT_THAT(Describe(matcher), "matches text");
  EXPECT_THAT(DescribeNegation(matcher), "does not match text");
  EXPECT_THAT("", EqualsText(""));
  EXPECT_THAT("", Not(EqualsText("abc")));
  EXPECT_THAT("abc", Not(EqualsText("")));
  EXPECT_THAT("abc", EqualsText("abc"));
  EXPECT_THAT(MatchAndExplain(EqualsText("a\nb\nc"), "a\nb\nc"), Pair(true, ""));
  EXPECT_THAT(MatchAndExplain(EqualsText("a\nb\nc"), "a\nX\nc"), Pair(false, EqualsText(R"(Text difference:
@@ -1,3 +1,3 @@
 a
-b
+X
 c
)")));
  EXPECT_THAT(MatchAndExplain(EqualsText("a\nb\nc"), "a\nX\nc"), Pair(false, WithDropIndent(EqualsText(R"(
    Text difference:
    @@ -1,3 +1,3 @@
     a
    -b
    +X
     c
    )"))));
}

// NOLINTEND(*-magic-numbers)

}  // namespace
}  // namespace mbo::testing
