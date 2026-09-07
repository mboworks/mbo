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

#include "mbo/container/any_scan.h"

#include <array>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/hash/hash.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/limited_set.h"
#include "mbo/container/limited_vector.h"
#include "mbo/types/traits.h"

namespace std {

template<>
struct hash<std::pair<std::string, std::string>> {  // NOLINT(cert-dcl58-cpp)

  std::size_t operator()(const std::pair<std::string, std::string>& obj) const noexcept {
    return absl::HashOf(obj.first, obj.second);
  }
};

}  // namespace std

namespace mbo::container {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::Pair;
using ::testing::Pointee;
using ::testing::SizeIs;
using ::testing::StrEq;
using ::testing::WhenSorted;

static_assert(std::input_iterator<AnyScan<int, std::ptrdiff_t>::iterator>);
static_assert(std::input_iterator<AnyScan<int, std::ptrdiff_t>::const_iterator>);
static_assert(std::input_iterator<ConstScan<int, std::ptrdiff_t>::iterator>);
static_assert(std::input_iterator<ConstScan<int, std::ptrdiff_t>::const_iterator>);
static_assert(std::input_iterator<ConvertingScan<int, std::ptrdiff_t>::iterator>);
static_assert(std::input_iterator<ConvertingScan<int, std::ptrdiff_t>::const_iterator>);
static_assert(!std::forward_iterator<AnyScan<int, std::ptrdiff_t>::const_iterator>);
static_assert(!mbo::types::ContainerIsForwardIteratable<AnyScan<int, std::ptrdiff_t>>);

struct AnyScanTest : public ::testing::Test {};

struct ConstScanTest : public AnyScanTest {};

template<typename T>
std::vector<std::remove_const_t<T>> Tester(const AnyScan<T>& scan) {
  std::vector<std::remove_const_t<T>> result{scan.begin(), scan.end()};
  EXPECT_THAT(scan.size(), result.size());
  EXPECT_THAT(scan.empty(), result.empty());
  return result;
}

template<typename T>
std::vector<std::remove_const_t<T>> ConstTester(const ConstScan<T>& scan) {
  std::vector<std::remove_const_t<T>> result{scan.begin(), scan.end()};
  EXPECT_THAT(scan.size(), result.size());
  EXPECT_THAT(scan.empty(), result.empty());
  return result;
}

TEST_F(AnyScanTest, TestInitializerList) {
  std::initializer_list<int> empty{};
  EXPECT_THAT(Tester<const int>(MakeAnyScan(empty)), ElementsAre());
  EXPECT_THAT(Tester<const int>(MakeAnyScan(empty)), IsEmpty());
  EXPECT_THAT(Tester<const int>(MakeAnyScan(empty)), SizeIs(0));
  std::initializer_list<int> data{1, 2, 3};
  EXPECT_THAT(Tester<const int>(MakeAnyScan(data)), ElementsAre(1, 2, 3));
  EXPECT_THAT(Tester<const int>(MakeAnyScan(data)), Not(IsEmpty()));
  EXPECT_THAT(Tester<const int>(MakeAnyScan(data)), SizeIs(3));
}

TEST_F(AnyScanTest, ReferenceIteratorsCompareByPosition) {
  std::array<int, 3> data{1, 2, 3};
  AnyScan<int> scan(MakeAnyScan(data));
  auto first = scan.begin();
  auto same = scan.begin();
  auto second = scan.begin();
  ++second;

  EXPECT_THAT(first == same, IsTrue());
  EXPECT_THAT(first == second, IsFalse());
}

TEST_F(AnyScanTest, DefaultIteratorsAreAtEnd) {
  const AnyScan<int>::iterator first;
  const AnyScan<int>::iterator second;

  EXPECT_THAT(first == second, IsTrue());
}

TEST_F(ConstScanTest, TestInitializerList) {
  std::initializer_list<int> data{1, 2, 3};
  EXPECT_THAT(ConstTester<int>(data), ElementsAre(1, 2, 3));
  EXPECT_THAT(ConstTester<int>(MakeConstScan(data)), ElementsAre(1, 2, 3));
}

TEST_F(AnyScanTest, CallFunction) {
  using Values = int;
  EXPECT_THAT(Tester<Values>(MakeAnyScan(std::array<Values, 3>{1, 2, 3})), ElementsAre(1, 2, 3));
  EXPECT_THAT(Tester<Values>(MakeAnyScan(std::array<Values, 3>{1, 2, 3})), ElementsAre(1, 2, 3));
  EXPECT_THAT(Tester<Values>(MakeAnyScan(LimitedVector<Values, 3>{1, 2, 3})), ElementsAre(1, 2, 3));
  EXPECT_THAT(Tester<Values>(MakeAnyScan(std::list<Values>{1, 2, 3})), ElementsAre(1, 2, 3));
  EXPECT_THAT(Tester<Values>(MakeAnyScan(std::vector<Values>{1, 2, 3})), ElementsAre(1, 2, 3));
}

TEST_F(AnyScanTest, CallFunctionConstKeyed) {
  using Values = int;
  EXPECT_THAT(Tester<const Values>(MakeAnyScan(std::set<Values>{1, 2, 3})), ElementsAre(1, 2, 3));
  EXPECT_THAT(Tester<const Values>(MakeAnyScan(std::unordered_set<Values>{1, 2, 3})), WhenSorted(ElementsAre(1, 2, 3)));
}

TEST_F(ConstScanTest, CallFunction) {
  using Values = int;
  EXPECT_THAT(ConstTester<Values>(MakeConstScan(LimitedSet<Values, 3>{1, 2, 3})), ElementsAre(1, 2, 3));
  EXPECT_THAT(ConstTester<Values>(MakeConstScan(LimitedSet<Values, 3>{1, 2, 3})), ElementsAre(1, 2, 3));
  EXPECT_THAT(ConstTester<Values>(MakeConstScan(LimitedVector<Values, 3>{1, 2, 3})), ElementsAre(1, 2, 3));
  EXPECT_THAT(ConstTester<Values>(MakeConstScan(std::list<Values>{1, 2, 3})), ElementsAre(1, 2, 3));
  EXPECT_THAT(ConstTester<Values>(MakeConstScan(std::vector<Values>{1, 2, 3})), ElementsAre(1, 2, 3));
  // const key'd
  EXPECT_THAT(ConstTester<Values>(MakeConstScan(std::set<Values>{1, 2, 3})), ElementsAre(1, 2, 3));
  EXPECT_THAT(
      ConstTester<Values>(MakeConstScan(std::unordered_set<Values>{1, 2, 3})), WhenSorted(ElementsAre(1, 2, 3)));
}

TEST_F(AnyScanTest, CallFunctionString) {
  using Values = std::string;
  EXPECT_THAT(Tester<Values>(MakeAnyScan(std::array<Values, 2>{"a", "b"})), ElementsAre("a", "b"));
  EXPECT_THAT(Tester<Values>(MakeAnyScan(LimitedSet<Values, 3>{"a", "b"})), ElementsAre("a", "b"));
  EXPECT_THAT(Tester<Values>(MakeAnyScan(LimitedVector<Values, 3>{"a", "b"})), ElementsAre("a", "b"));
  EXPECT_THAT(Tester<Values>(MakeAnyScan(std::list<Values>{"a", "b"})), ElementsAre("a", "b"));
  EXPECT_THAT(Tester<Values>(MakeAnyScan(std::vector<Values>{"a", "b"})), ElementsAre("a", "b"));
}

TEST_F(AnyScanTest, CallFunctionStringConstKeyed) {
  using Values = std::string;
  EXPECT_THAT(Tester<const Values>(MakeAnyScan(std::set<Values>{"a", "b"})), ElementsAre("a", "b"));
  EXPECT_THAT(
      Tester<const Values>(MakeAnyScan(std::unordered_set<Values>{"a", "b"})), WhenSorted(ElementsAre("a", "b")));
}

TEST_F(ConstScanTest, CallFunctionString) {
  using Values = std::string;
  EXPECT_THAT(ConstTester<Values>(MakeConstScan(std::array<Values, 2>{"a", "b"})), ElementsAre("a", "b"));
  EXPECT_THAT(ConstTester<Values>(MakeConstScan(LimitedSet<Values, 3>{"a", "b"})), ElementsAre("a", "b"));
  EXPECT_THAT(ConstTester<Values>(MakeConstScan(LimitedVector<Values, 3>{"a", "b"})), ElementsAre("a", "b"));
  EXPECT_THAT(ConstTester<Values>(MakeConstScan(std::list<Values>{"a", "b"})), ElementsAre("a", "b"));
  EXPECT_THAT(ConstTester<Values>(MakeConstScan(std::vector<Values>{"a", "b"})), ElementsAre("a", "b"));
  // Const key'd
  EXPECT_THAT(ConstTester<Values>(MakeConstScan(std::set<Values>{"a", "b"})), ElementsAre("a", "b"));
  EXPECT_THAT(
      ConstTester<Values>(MakeConstScan(std::unordered_set<Values>{"a", "b"})), WhenSorted(ElementsAre("a", "b")));
}

TEST_F(AnyScanTest, CallFunctionPairOfStrings) {
  using Values = std::pair<std::string, std::string>;
  EXPECT_THAT(
      Tester<Values>(MakeAnyScan(std::array<Values, 2>{{{"1", "a"}, {"2", "b"}}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
  EXPECT_THAT(
      Tester<Values>(MakeAnyScan(LimitedSet<Values, 3>{{"1", "a"}, {"2", "b"}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
  EXPECT_THAT(
      Tester<Values>(MakeAnyScan(LimitedVector<Values, 3>{{"1", "a"}, {"2", "b"}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
  EXPECT_THAT(
      Tester<Values>(MakeAnyScan(std::list<Values>{{"1", "a"}, {"2", "b"}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
  EXPECT_THAT(
      Tester<Values>(MakeAnyScan(std::vector<Values>{{"1", "a"}, {"2", "b"}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
}

TEST_F(AnyScanTest, CallFunctionPairOfStringsConstKeyed) {
  using Values = std::pair<std::string, std::string>;
  EXPECT_THAT(
      Tester<const Values>(MakeAnyScan(std::set<Values>{{"1", "a"}, {"2", "b"}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
  EXPECT_THAT(
      Tester<const Values>(MakeAnyScan(std::unordered_set<Values>{{"1", "a"}, {"2", "b"}})),
      WhenSorted(ElementsAre(Pair("1", "a"), Pair("2", "b"))));
}

TEST_F(AnyScanTest, CallFunctionPairOfStringsWithMap) {
  using Values = std::pair<const std::string, std::string>;
  // std::map's iterator has type `std::pair<const Key, Value>`, meaning its first type (Key) is const.
  EXPECT_THAT(
      Tester<Values>(MakeAnyScan(std::map<std::string, std::string>{{"1", "a"}, {"2", "b"}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
}

TEST_F(ConstScanTest, CallFunctionPairOfStrings) {
  using Values = std::pair<std::string, std::string>;
  EXPECT_THAT(
      ConstTester<Values>(MakeConstScan(std::array<Values, 2>{{{"1", "a"}, {"2", "b"}}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
  EXPECT_THAT(
      ConstTester<Values>(MakeConstScan(LimitedSet<Values, 3>{{"1", "a"}, {"2", "b"}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
  EXPECT_THAT(
      ConstTester<Values>(MakeConstScan(LimitedVector<Values, 3>{{"1", "a"}, {"2", "b"}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
  EXPECT_THAT(
      ConstTester<Values>(MakeConstScan(std::list<Values>{{"1", "a"}, {"2", "b"}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
  EXPECT_THAT(
      ConstTester<Values>(MakeConstScan(std::vector<Values>{{"1", "a"}, {"2", "b"}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
  // const key'd
  EXPECT_THAT(
      ConstTester<Values>(MakeConstScan(std::set<Values>{{"1", "a"}, {"2", "b"}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
  EXPECT_THAT(
      ConstTester<Values>(MakeConstScan(std::unordered_set<Values>{{"1", "a"}, {"2", "b"}})),
      WhenSorted(ElementsAre(Pair("1", "a"), Pair("2", "b"))));
  using MapValues = std::pair<const std::string, std::string>;
  EXPECT_THAT(
      ConstTester<MapValues>(MakeConstScan(std::map<std::string, std::string>{{"1", "a"}, {"2", "b"}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
}

TEST_F(AnyScanTest, InitializerList) {
  EXPECT_THAT(Tester<const std::string_view>({"foo", "bar"}), ElementsAre("foo", "bar"));
  EXPECT_THAT(Tester<const std::string>({"foo", "bar"}), ElementsAre("foo", "bar"));
  std::initializer_list<std::string_view> data{"foo", "bar"};
  EXPECT_THAT(Tester<const std::string_view>(MakeAnyScan(data)), ElementsAre("foo", "bar"));
}

template<typename T>
std::vector<T> MoveTester(AnyScan<T> scan) {
  const std::size_t expected_size = scan.size();
  const bool expected_empty = scan.empty();
  std::vector<T> result;
  for (typename AnyScan<T>::iterator it = scan.begin(); it != scan.end(); ++it) {
    result.emplace_back(std::move(*it));
  }
  EXPECT_THAT(result, SizeIs(expected_size));
  EXPECT_THAT(result.empty(), expected_empty);
  return result;
}

TEST_F(AnyScanTest, MoveOnly) {
  auto data = [] {
    std::list<std::unique_ptr<std::string>> data;
    data.emplace_back(std::make_unique<std::string>("foo"));
    data.emplace_back(std::make_unique<std::string>("bar"));
    return data;
  };
  EXPECT_THAT(
      MoveTester<std::unique_ptr<std::string>>(MakeAnyScan(data())),
      ElementsAre(Pointee(StrEq("foo")), Pointee(StrEq("bar"))));
}

struct ConvertingScanTest : public AnyScanTest {};

template<typename T>
std::vector<std::remove_cvref_t<T>> ConvTester(
    const ConvertingScan<T>& scan) {  // NOLINT(portability-std-allocator-const
  std::vector<std::remove_cvref_t<T>> result{scan.begin(), scan.end()};
  EXPECT_THAT(result, SizeIs(scan.size()));
  EXPECT_THAT(result.empty(), scan.empty());
  return result;
}

TEST_F(ConvertingScanTest, CallFunctionPairOfStringsWithMap) {
  using Values = std::pair<std::string, std::string>;
  // std::map's iterator has type std::pair<sonst...>, meaning its first type is const.
  EXPECT_THAT(
      ConvTester<Values>(MakeConvertingScan(std::map<std::string, std::string>{{"1", "a"}, {"2", "b"}})),
      ElementsAre(Pair("1", "a"), Pair("2", "b")));
}

TEST_F(ConvertingScanTest, CallFunctionWithConversion) {
  {
    const std::array<std::string, 3> data{{"foo", "bar", "baz"}};
    EXPECT_THAT(ConvTester<std::string_view>(MakeConvertingScan(data)), ElementsAre("foo", "bar", "baz"));
  }
  {
    const std::array<std::string_view, 3> data{{"foo", "bar", "baz"}};
    EXPECT_THAT(ConvTester<std::string>(MakeConvertingScan(data)), ElementsAre("foo", "bar", "baz"));
  }
  {
    const std::array<std::string, 3> data{{"foo", "bar", "baz"}};
    EXPECT_THAT(ConvTester<std::string>(MakeConvertingScan(data)), ElementsAre("foo", "bar", "baz"));
  }
  {
    const std::array<std::string_view, 3> data{{"foo", "bar", "baz"}};
    EXPECT_THAT(ConvTester<std::string_view>(MakeConvertingScan(data)), ElementsAre("foo", "bar", "baz"));
  }
}

TEST_F(ConvertingScanTest, IteratorsOnlyCompareEqualToThemselves) {
  const std::array<std::string_view, 2> data{{"foo", "bar"}};
  const ConvertingScan<std::string> scan = MakeConvertingScan(data);
  auto first = scan.begin();
  const auto same_position = scan.begin();
  EXPECT_THAT(first == first, IsTrue());
  EXPECT_THAT(first == same_position, IsFalse());
}

TEST_F(ConvertingScanTest, InitializerList) {
  EXPECT_THAT(ConvTester<std::string_view>({"foo", "bar"}), ElementsAre("foo", "bar"));
  EXPECT_THAT(ConvTester<std::string>({"foo", "bar"}), ElementsAre("foo", "bar"));
  EXPECT_THAT(ConvTester<std::string>({"foo", "bar"}), SizeIs(2));
  EXPECT_THAT(ConvTester<std::string>({"foo", "bar"}), Not(IsEmpty()));
}

TEST_F(ConvertingScanTest, StringPairs) {
  // initializer_list - non-const pair of non-const value
  {
    EXPECT_THAT(  // pair of string_view
        (ConvTester<std::pair<std::string_view, std::string_view>>({{"foo", "25"}, {"bar", "42"}})),
        ElementsAre(Pair("foo", "25"), Pair("bar", "42")));
    EXPECT_THAT(  // Same with pair of strings
        (ConvTester<std::pair<std::string, std::string>>({{"foo", "25"}, {"bar", "42"}})),
        ElementsAre(Pair("foo", "25"), Pair("bar", "42")));
  }
  // initializer_list - non-const pair of const & non-const value
  {
    EXPECT_THAT(  // pair of string_view
        (ConvTester<std::pair<const std::string_view, std::string_view>>({{"foo", "25"}, {"bar", "42"}})),
        ElementsAre(Pair("foo", "25"), Pair("bar", "42")));
    EXPECT_THAT(  // Same with pair of strings
        (ConvTester<std::pair<const std::string, std::string>>({{"foo", "25"}, {"bar", "42"}})),
        ElementsAre(Pair("foo", "25"), Pair("bar", "42")));
  }
  // initializer_list - const pair of const & non-const value
  {
    EXPECT_THAT(  // pair of string_view
        (ConvTester<const std::pair<const std::string_view, std::string_view>>({{"foo", "25"}, {"bar", "42"}})),
        ElementsAre(Pair("foo", "25"), Pair("bar", "42")));
    EXPECT_THAT(  // Same with pair of strings
        (ConvTester<const std::pair<const std::string, std::string>>({{"foo", "25"}, {"bar", "42"}})),
        ElementsAre(Pair("foo", "25"), Pair("bar", "42")));
  }
  // map - non-const pair of non-const value
  {
    EXPECT_THAT(  // pair of string_view
        (ConvTester<std::pair<std::string_view, std::string_view>>(
            MakeConvertingScan(std::map<std::string_view, std::string_view>{{"foo", "25"}, {"bar", "42"}}))),
        ElementsAre(Pair("bar", "42"), Pair("foo", "25")));
    EXPECT_THAT(  // Same with pair of strings
        (ConvTester<std::pair<std::string, std::string>>(
            MakeConvertingScan(std::map<std::string, std::string>{{"foo", "25"}, {"bar", "42"}}))),
        ElementsAre(Pair("bar", "42"), Pair("foo", "25")));
  }
  // map - non-const pair of const & non-const value
  {
    EXPECT_THAT(  // pair of string_view
        (ConvTester<std::pair<const std::string_view, std::string_view>>(
            MakeConvertingScan(std::map<std::string_view, std::string_view>{{"foo", "25"}, {"bar", "42"}}))),
        ElementsAre(Pair("bar", "42"), Pair("foo", "25")));
    EXPECT_THAT(  // Same with pair of strings
        (ConvTester<std::pair<const std::string, std::string>>(
            MakeConvertingScan(std::map<std::string, std::string>{{"foo", "25"}, {"bar", "42"}}))),
        ElementsAre(Pair("bar", "42"), Pair("foo", "25")));
  }
  // map - const pair of const & non-const value
  {
    EXPECT_THAT(  // pair of string_view
        (ConvTester<const std::pair<const std::string_view, std::string_view>>(
            MakeConvertingScan(std::map<std::string_view, std::string_view>{{"foo", "25"}, {"bar", "42"}}))),
        ElementsAre(Pair("bar", "42"), Pair("foo", "25")));
    EXPECT_THAT(  // Same with pair of strings
        (ConvTester<const std::pair<const std::string, std::string>>(
            MakeConvertingScan(std::map<std::string, std::string>{{"foo", "25"}, {"bar", "42"}}))),
        ElementsAre(Pair("bar", "42"), Pair("foo", "25")));
  }
}

}  // namespace
}  // namespace mbo::container
