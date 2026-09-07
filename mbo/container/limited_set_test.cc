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

#include "mbo/container/limited_set.h"

#include <array>
#include <cstddef>
#include <functional>
#include <ranges>     // IWYU pragma: keep
#include <stdexcept>  // IWYU pragma: keep
#include <string>
#include <string_view>
#include <type_traits>  // IWYU pragma: keep
#include <utility>
#include <vector>

#include "absl/log/initialize.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/config/config.h"
#include "mbo/container/limited_options.h"
#include "mbo/testing/matchers.h"
#include "mbo/types/compare.h"

// Clang has issues with exception tracing in ASAN, so corresponding tests must
// be disabled. But we do so for all known ASAN identification methods.
#ifndef HAS_ADDRESS_SANITIZER
# if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#   define HAS_ADDRESS_SANITIZER 1
#  endif
# elif defined(__SANITIZE_ADDRESS__)
#  define HAS_ADDRESS_SANITIZER 1
# endif
#endif

namespace mbo::container {
namespace {

// NOLINTBEGIN(*-magic-numbers)

using ::mbo::testing::CapacityIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::Le;
using ::testing::Lt;
using ::testing::Ne;
using ::testing::Not;
using ::testing::Pair;
using ::testing::SizeIs;

static_assert(std::ranges::range<LimitedSet<int, 1>>);
static_assert(std::contiguous_iterator<LimitedSet<int, 2>::iterator>);
static_assert(std::contiguous_iterator<LimitedSet<int, 3>::const_iterator>);

struct LimitedSetTest : ::testing::Test {
  static void SetUpTestSuite() { absl::InitializeLog(); }
};

TEST_F(LimitedSetTest, MakeNoArg) {
  constexpr auto kTest = MakeLimitedSet<int>();
  const auto runtime_test = MakeLimitedSet<int>();
  EXPECT_THAT(kTest, IsEmpty());
  EXPECT_THAT(kTest, SizeIs(0));
  EXPECT_THAT(kTest, CapacityIs(0));
  EXPECT_THAT(kTest, ElementsAre());
  EXPECT_THAT(runtime_test, IsEmpty());
}

TEST_F(LimitedSetTest, MakeOneArg) {
  constexpr auto kTest = MakeLimitedSet(42);
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(1));
  EXPECT_THAT(kTest, CapacityIs(1));
  EXPECT_THAT(kTest, ElementsAre(42));
}

TEST_F(LimitedSetTest, MakeInitArgCTAD) {
  {
    constexpr LimitedSet kTest{1};
    EXPECT_THAT(kTest, Not(IsEmpty()));
    EXPECT_THAT(kTest, SizeIs(1));
    EXPECT_THAT(kTest, CapacityIs(1));
    EXPECT_THAT(kTest, ElementsAre(1));
  }
  {
    constexpr LimitedSet kTest{1, 2};
    EXPECT_THAT(kTest, Not(IsEmpty()));
    EXPECT_THAT(kTest, SizeIs(2));
    EXPECT_THAT(kTest, CapacityIs(2));
    EXPECT_THAT(kTest, ElementsAre(1, 2));
  }
  {
    constexpr LimitedSet kTest{1, 2, 3};
    EXPECT_THAT(kTest, Not(IsEmpty()));
    EXPECT_THAT(kTest, SizeIs(3));
    EXPECT_THAT(kTest, CapacityIs(3));
    EXPECT_THAT(kTest, ElementsAre(1, 2, 3));
  }
  {
    constexpr LimitedSet kTest{"a", "b", "c", "d"};
    EXPECT_THAT(kTest, Not(IsEmpty()));
    EXPECT_THAT(kTest, SizeIs(4));
    EXPECT_THAT(kTest, CapacityIs(4));
    EXPECT_THAT(kTest, ElementsAre("a", "b", "c", "d"));
  }
}

TEST_F(LimitedSetTest, MakeInitArgFind) {
  constexpr auto kTest = MakeLimitedSet<5>({1, 3, 5});
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(3));
  EXPECT_THAT(kTest, CapacityIs(5));
  EXPECT_THAT(kTest, ElementsAre(1, 3, 5));
  EXPECT_THAT(kTest.find(1) - kTest.begin(), 0);
  EXPECT_THAT(kTest.find(3) - kTest.begin(), 1);
  EXPECT_THAT(kTest.find(5) - kTest.begin(), 2);
  EXPECT_THAT(kTest.find(0), kTest.end());
  EXPECT_THAT(kTest.find(2), kTest.end());
}

TEST_F(LimitedSetTest, MakeInitArgBasics) {
  auto test = MakeLimitedSet<7>({1, 3, 5});
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(3));
  EXPECT_THAT(test, CapacityIs(7));
  EXPECT_THAT(test, ElementsAre(1, 3, 5));
  EXPECT_THAT(test.end() - test.begin(), 3);
  EXPECT_THAT(test.find(1), test.begin());
  EXPECT_THAT(test.find(1) - test.begin(), 0);
  EXPECT_THAT(test.find(3), test.begin() + 1);
  EXPECT_THAT(test.find(3) - test.begin(), 1);
  EXPECT_THAT(test.find(5), test.begin() + 2);
  EXPECT_THAT(test.find(5), test.end() - 1);
  EXPECT_THAT(test.find(5) - test.begin(), 2);
  EXPECT_THAT(test.find(0), test.end());
  EXPECT_THAT(test.emplace(0), Pair(test.begin(), true));
  EXPECT_THAT(test.end() - test.begin(), 4);
  EXPECT_THAT(test, ElementsAre(0, 1, 3, 5));
  EXPECT_THAT(test.find(2), test.end());
  EXPECT_THAT(test.emplace(2), Pair(test.begin() + 2, true));
  EXPECT_THAT(test.end() - test.begin(), 5);
  EXPECT_THAT(test, ElementsAre(0, 1, 2, 3, 5));
  EXPECT_THAT(test.find(6), test.end());
  EXPECT_THAT(test.emplace(6), Pair(test.end(), true));
  EXPECT_THAT(test, ElementsAre(0, 1, 2, 3, 5, 6));
  EXPECT_THAT(test.find(4), test.end());
  EXPECT_THAT(test.emplace(4), Pair(test.begin() + 4, true));
  EXPECT_THAT(test, ElementsAre(0, 1, 2, 3, 4, 5, 6));
  EXPECT_THAT(test.find(0), test.begin());
  EXPECT_THAT(test.find(1), test.begin() + 1);
  EXPECT_THAT(test.find(2), test.begin() + 2);
  EXPECT_THAT(test.find(3), test.begin() + 3);
  EXPECT_THAT(test.find(4), test.begin() + 4);
  EXPECT_THAT(test.find(5), test.begin() + 5);
  EXPECT_THAT(test.find(6), test.begin() + 6);
}

TEST_F(LimitedSetTest, MakeInitWithDuplicates) {
  auto test = MakeLimitedSet<3>({1, 3, 3, 3, 5});
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(3));
  EXPECT_THAT(test, CapacityIs(3)) << "There are duplicates, and so the construction with M=3 works.";
  EXPECT_THAT(test, ElementsAre(1, 3, 5));
}

TEST_F(LimitedSetTest, MakeInitArg) {
  constexpr auto kTest = MakeLimitedSet<3>({1, 2, 0});
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(3));
  EXPECT_THAT(kTest, CapacityIs(3));
  EXPECT_THAT(kTest, ElementsAre(0, 1, 2));
}

TEST_F(LimitedSetTest, MakeInitArgLarger) {
  constexpr auto kTest = MakeLimitedSet<5>({1, 0, 2});
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(3));
  EXPECT_THAT(kTest, CapacityIs(5));
  EXPECT_THAT(kTest, ElementsAre(0, 1, 2));
}

TEST_F(LimitedSetTest, MakeMultiArg) {
  constexpr auto kTest = MakeLimitedSet(0, 3, 2, 1);
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(4));
  EXPECT_THAT(kTest, CapacityIs(4));
  EXPECT_THAT(kTest, ElementsAre(0, 1, 2, 3));
}

TEST_F(LimitedSetTest, CustomCompare) {
  constexpr auto kTest = MakeLimitedSet<4>({0, 3, 2, 1}, std::greater());
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(4));
  EXPECT_THAT(kTest, CapacityIs(4));
  EXPECT_THAT(kTest, ElementsAre(3, 2, 1, 0));
}

TEST_F(LimitedSetTest, MakeIteratorArg) {
  constexpr std::array<int, 4> kVec{0, 1, 2, 3};
  constexpr auto kTest = MakeLimitedSet<5>(kVec.begin(), kVec.end());
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(4));
  EXPECT_THAT(kTest, CapacityIs(5));
  EXPECT_THAT(kTest, ElementsAre(0, 1, 2, 3));
}

TEST_F(LimitedSetTest, MakeWithStrings) {
  const std::vector<std::string> data{{"0"}, {"1"}, {"2"}, {"3"}};
  auto test = MakeLimitedSet<4>(data.begin(), data.end());
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(4));
  EXPECT_THAT(test, CapacityIs(4));
  EXPECT_THAT(test, ElementsAre("0", "1", "2", "3"));
}

TEST_F(LimitedSetTest, MakeWithStringConversions) {
  class Str {
   public:
    Str() = delete;

    explicit Str(std::string_view str) : str_(str) {}

    explicit operator std::string() const { return str_; }

   private:
    const std::string str_;
  };

  const auto elements_are = ElementsAre("0", "1", "2", "3");
  {
    const std::initializer_list<Str> data{Str{"0"}, Str{"1"}, Str{"2"}, Str{"3"}};
    const LimitedSet<std::string, 4> test(data);
    EXPECT_THAT(test, Not(IsEmpty()));
    EXPECT_THAT(test, SizeIs(4));
    EXPECT_THAT(test, CapacityIs(4));
    EXPECT_THAT(test, elements_are);
  }
  {
    const auto test = MakeLimitedSetOf<std::string>(Str{"0"}, Str{"1"}, Str{"2"}, Str{"3"});
    EXPECT_THAT(test, elements_are);
  }
}

TEST_F(LimitedSetTest, ConstructAssignFromSmaller) {
  {
    constexpr LimitedSet<unsigned, 3> kSource({0U, 1U, 2U});
    const LimitedSet<int, 5> target(kSource);
    EXPECT_THAT(target, ElementsAre(0, 1, 2));
  }
  {
    constexpr LimitedSet<unsigned, 3> kSource({0U, 1U, 2U});
    LimitedSet<int, 5> target;
    ASSERT_THAT(target, IsEmpty());
    target = kSource;
    EXPECT_THAT(target, ElementsAre(0, 1, 2));
  }
  {
    LimitedSet<unsigned, 4> source({0U, 1U, 2U});
    const LimitedSet<int, 5> target(std::move(source));
    EXPECT_THAT(target, ElementsAre(0, 1, 2));
  }
  {
    const LimitedSet<unsigned, 3> source({0U, 1U, 2U});
    LimitedSet<int, 5> target;
    ASSERT_THAT(target, IsEmpty());
    target = source;
    EXPECT_THAT(target, ElementsAre(0, 1, 2));
  }
}

TEST_F(LimitedSetTest, ToLimitedSet) {
  // NOLINTBEGIN(*-avoid-c-arrays)
  constexpr int kArray[4] = {0, 1, 2, 3};
  constexpr auto kTest = ToLimitedSet(kArray);
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(4));
  EXPECT_THAT(kTest, CapacityIs(4));
  EXPECT_THAT(kTest, ElementsAre(0, 1, 2, 3));
  // NOLINTEND(*-avoid-c-arrays)
}

TEST_F(LimitedSetTest, ToLimitedSetStringCopy) {
  // NOLINTBEGIN(*-avoid-c-arrays)
  const std::string array[4] = {{"0"}, {"1"}, {"2"}, {"3"}};
  auto test = ToLimitedSet(array);
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(4));
  EXPECT_THAT(test, CapacityIs(4));
  EXPECT_THAT(test, ElementsAre("0", "1", "2", "3"));
  // NOLINTEND(*-avoid-c-arrays)
}

TEST_F(LimitedSetTest, ToLimitedSetStringMove) {
  // NOLINTBEGIN(*-avoid-c-arrays)
  const std::string array[4] = {{"0"}, {"1"}, {"2"}, {"3"}};
  auto test = ToLimitedSet(array);
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(4));
  EXPECT_THAT(test, CapacityIs(4));
  EXPECT_THAT(test, ElementsAre("0", "1", "2", "3"));
  // NOLINTEND(*-avoid-c-arrays)
}

TEST_F(LimitedSetTest, ConstexprMakeClear) {
  constexpr auto kTest = [] {
    auto test = MakeLimitedSet<5>({0, 1, 2});
    test.clear();
    return test;
  }();
  EXPECT_THAT(kTest, IsEmpty());
  EXPECT_THAT(kTest, SizeIs(0));
  EXPECT_THAT(kTest, CapacityIs(5));
  EXPECT_THAT(kTest, ElementsAre());
}

TEST_F(LimitedSetTest, Erase) {
  auto test = MakeLimitedSet(0, 1, 2, 3, 4);
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(5));
  EXPECT_THAT(test, CapacityIs(5));
  ASSERT_THAT(test, ElementsAre(0, 1, 2, 3, 4));
  EXPECT_THAT(test.erase(test.begin() + 2), test.begin() + 2);
  EXPECT_THAT(test, SizeIs(4));
  EXPECT_THAT(test, ElementsAre(0, 1, 3, 4));
  EXPECT_THAT(test.erase(test.end() - 1), test.begin() + 3);
  EXPECT_THAT(test.begin() + 3, test.end()) << "Should have returned new `end`.";
  EXPECT_THAT(test, SizeIs(3));
  EXPECT_THAT(test, ElementsAre(0, 1, 3));
  EXPECT_THAT(test.erase(1), 1);
  EXPECT_THAT(test.erase(1), 0);
  EXPECT_THAT(test, SizeIs(2));
  EXPECT_THAT(test, ElementsAre(0, 3));
  EXPECT_THAT(test.erase(test.begin()), test.begin());
  EXPECT_THAT(test.erase(test.begin()), test.begin());
  EXPECT_THAT(test.begin(), test.end()) << "Should have returned new `end`.";
  EXPECT_THAT(test, IsEmpty());
}

TEST_F(LimitedSetTest, Contains) {
  constexpr auto kTest = MakeLimitedSet<6>({0, 1, 2, 3});
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(4));
  EXPECT_THAT(kTest, CapacityIs(6));
  ASSERT_THAT(kTest, ElementsAre(0, 1, 2, 3));
  EXPECT_THAT(kTest.contains(0), true);
  EXPECT_THAT(kTest.contains(4), false);
  EXPECT_THAT(kTest.contains_all(std::vector<int>{1, 2}), true);
  EXPECT_THAT(kTest.contains_all({1, 5}), false);
  EXPECT_THAT(kTest.contains_any(std::vector<int>{5, 2}), true);
  EXPECT_THAT(kTest.contains_any({4, 5}), false);
}

TEST_F(LimitedSetTest, Insert) {
  auto test = MakeLimitedSet<6>({0, 3});
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(2));
  EXPECT_THAT(test, CapacityIs(6));
  ASSERT_THAT(test, ElementsAre(0, 3));
  const std::vector<int> other{1, 2, 4};
  test.insert(other.begin(), other.end());
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(5));
  EXPECT_THAT(test, CapacityIs(6));
  ASSERT_THAT(test, ElementsAre(0, 1, 2, 3, 4));
}

TEST_F(LimitedSetTest, Swap) {
  auto test1 = MakeLimitedSet<int>(0, 1, 2);
  auto test2 = LimitedSet<int, 3>({3});
  ASSERT_THAT(test1, ElementsAre(0, 1, 2));
  ASSERT_THAT(test2, ElementsAre(3));
  test1.swap(test2);
  EXPECT_THAT(test1, ElementsAre(3));
  EXPECT_THAT(test2, ElementsAre(0, 1, 2));
  test1.swap(test2);
  EXPECT_THAT(test1, ElementsAre(0, 1, 2));
  EXPECT_THAT(test2, ElementsAre(3));
  test2.clear();
  test1.swap(test2);
  EXPECT_THAT(test1, ElementsAre());
  EXPECT_THAT(test2, ElementsAre(0, 1, 2));
  test2.clear();
  test1.swap(test2);
  EXPECT_THAT(test1, ElementsAre());
  EXPECT_THAT(test2, ElementsAre());
}

TEST_F(LimitedSetTest, Iterators) {
  constexpr auto kTest = MakeLimitedSet(0, 1, 2);
  // Restrictions apply: The two following cannot be constexpr.
  EXPECT_THAT((MakeLimitedSet<3>(kTest.begin(), kTest.end())), ElementsAre(0, 1, 2));
  EXPECT_THAT((MakeLimitedSet<3>(kTest.rbegin(), kTest.rend())), ElementsAre(0, 1, 2));
}

TEST_F(LimitedSetTest, Compare) {
  constexpr auto k42v65 = MakeLimitedSet(42, 65);
  constexpr auto k42o65 = MakeLimitedSet(42, 65);
  constexpr auto k42v99 = MakeLimitedSet(42, 99);
  constexpr auto k42 = MakeLimitedSet(42);
  EXPECT_THAT(k42v65 == k42o65, true);
  EXPECT_THAT(k42v65, k42o65);
  EXPECT_THAT(k42v65, Eq(k42o65));

  EXPECT_THAT(k42v65 != k42v99, true);
  EXPECT_THAT(k42v65, Not(k42v99));
  EXPECT_THAT(k42v65, Not(Eq(k42v99)));
  EXPECT_THAT(k42v65 != k42, true);
  EXPECT_THAT(k42v65, Not(k42));
  EXPECT_THAT(k42v65, Not(Eq(k42)));

  EXPECT_THAT(k42v65 < k42v99, true);
  EXPECT_THAT(k42v65, Lt(k42v99));
  EXPECT_THAT(k42 < k42v99, true);
  EXPECT_THAT(k42, Lt(k42v99));
  EXPECT_THAT(k42v99 < k42v65, false);
  EXPECT_THAT(k42v99, Not(Lt(k42v65)));
  EXPECT_THAT(k42v99 < k42, false);
  EXPECT_THAT(k42v99, Not(Lt(k42)));

  EXPECT_THAT(k42v65 <= k42v65, true);
  EXPECT_THAT(k42v65, Le(k42v65));
  EXPECT_THAT(k42v65 <= k42v99, true);
  EXPECT_THAT(k42v65, Le(k42v99));
  EXPECT_THAT(k42 <= k42v99, true);
  EXPECT_THAT(k42, Le(k42v99));

  EXPECT_THAT(k42v99 > k42v65, true);
  EXPECT_THAT(k42v99, Gt(k42v65));

  EXPECT_THAT(k42v65 >= k42, true);
  EXPECT_THAT(k42v65, Ge(k42));
}

TEST_F(LimitedSetTest, CompareDifferentType) {
  const auto k42v65 = MakeLimitedSet<std::string>("42", "65");
  constexpr auto k42o65 = MakeLimitedSet<std::string_view>("42", "65");
  constexpr auto k42v99 = MakeLimitedSet("42", "99");
  constexpr auto k42 = MakeLimitedSet("42");
  EXPECT_THAT(k42v65 == k42o65, true);
  EXPECT_THAT(k42v65, k42o65);
  EXPECT_THAT(k42v65, Eq(k42o65));

  EXPECT_THAT(k42v65 != k42v99, true);
  EXPECT_THAT(k42v65, Not(k42v99));
  EXPECT_THAT(k42v65, Not(Eq(k42v99)));
  EXPECT_THAT(k42v65 != k42, true);
  EXPECT_THAT(k42v65, Not(k42));
  EXPECT_THAT(k42v65, Not(Eq(k42)));

  EXPECT_THAT(k42v65 < k42v99, true);
  EXPECT_THAT(k42v65, Lt(k42v99));
  EXPECT_THAT(k42 < k42v99, true);
  EXPECT_THAT(k42, Lt(k42v99));
  EXPECT_THAT(k42v99 < k42v65, false);
  EXPECT_THAT(k42v99, Not(Lt(k42v65)));
  EXPECT_THAT(k42v99 < k42, false);
  EXPECT_THAT(k42v99, Not(Lt(k42)));

  EXPECT_THAT(k42v65 <= k42v65, true);
  EXPECT_THAT(k42v65, Le(k42v65));
  EXPECT_THAT(k42v65 <= k42v99, true);
  EXPECT_THAT(k42v65, Le(k42v99));
  EXPECT_THAT(k42 <= k42v99, true);
  EXPECT_THAT(k42, Le(k42v99));

  EXPECT_THAT(k42v99 > k42v65, true);
  EXPECT_THAT(k42v99, Gt(k42v65));

  EXPECT_THAT(k42v65 >= k42, true);
  EXPECT_THAT(k42v65, Ge(k42));
}

template<std::size_t Size, template<typename> typename Compare, LimitedOptionsFlag... Flags>
void CompareAllTheSizesFor() {  // NOLINT(readability-function-cognitive-complexity)
  LimitedSet<int, LimitedOptions<Size, Flags...>{}, Compare<int>> data;  // NOLINT(modernize-use-transparent-functors)
  for (std::size_t len = 0; len < Size; ++len) {
    data.emplace(len * 100);
  }
  std::size_t dropped = 0;
  while (!data.empty()) {
    SCOPED_TRACE(absl::StrCat("Size: ", data.size()));
    for (std::size_t pos = 0; pos < Size + 1; ++pos) {
      const int v = static_cast<int>(100 * pos);
      const std::size_t expected_pos = [&data, &v] {  // NOLINT(modernize-use-transparent-functors)
        for (std::size_t pos = 0; pos < data.size(); ++pos) {
          if (data.at_index(pos) == v) {
            return pos;
          }
        }
        return data.npos;
      }();
      SCOPED_TRACE(absl::StrCat("Dropped: ", dropped, ", V: ", v, ", Expected: ", expected_pos));
      if (expected_pos == data.npos) {
        ASSERT_THAT(data.index_of(v), data.npos);
        ASSERT_FALSE(data.contains(v));
        ASSERT_THAT(data.find(v), data.end());
      } else {
        ASSERT_THAT(data.index_of(v), expected_pos);
        ASSERT_TRUE(data.contains(v));
        ASSERT_THAT(data.find(v), Ne(data.end()));
      }
    }
    data.erase(data.begin());
    ++dropped;
  }
}

template<template<typename> typename Compare, LimitedOptionsFlag Flags, std::size_t... Idx>
void CompareAllTheSizes(const std::index_sequence<Idx...>& /*unused*/) {
  (CompareAllTheSizesFor<Idx, Compare, Flags>(), ...);
}

template<template<typename> typename Compare, LimitedOptionsFlag Flags>
void CompareAllTheSizes() {
  CompareAllTheSizes<Compare, Flags>(std::make_index_sequence<50>());
}

// NOLINTBEGIN(google-readability-avoid-underscore-in-googletest-name)

TEST_F(LimitedSetTest, CompareAllTheSizes_StdLess_Default) {
  CompareAllTheSizes<std::less, LimitedOptionsFlag::kDefault>();
}

TEST_F(LimitedSetTest, CompareAllTheSizes_StdLess_NoOptimizeIndexOf) {
  CompareAllTheSizes<std::less, LimitedOptionsFlag::kNoOptimizeIndexOf>();
}

TEST_F(LimitedSetTest, CompareAllTheSizes_CompareLess_Default) {
  CompareAllTheSizes<mbo::types::CompareLess, LimitedOptionsFlag::kDefault>();
}

TEST_F(LimitedSetTest, CompareAllTheSizes_CompareLess_NoOptimizeIndexOf) {
  CompareAllTheSizes<mbo::types::CompareLess, LimitedOptionsFlag::kNoOptimizeIndexOf>();
}

// NOLINTEND(google-readability-avoid-underscore-in-googletest-name)

TEST_F(LimitedSetTest, PreSortedInput) {
  constexpr LimitedSet<int, LimitedOptions<4, LimitedOptionsFlag::kRequireSortedInput>{}> kData{0, 1, 2, 42};
  EXPECT_THAT(kData, ElementsAre(0, 1, 2, 42));
}

TEST_F(LimitedSetTest, AtIndex) {
  static constexpr auto kTest = LimitedSet<int, 2>{25, 42};
  EXPECT_THAT(kTest.at_index(0), 25);
  EXPECT_THAT(kTest.at_index(1), 42);
  auto test = LimitedSet<int, 2>{25, 42};
  test.at_index(1) = 99;
  EXPECT_THAT(test, ElementsAre(25, 99));
}

TEST_F(LimitedSetTest, AtIndexNonExistingThrows) {
  static constexpr auto kTest = LimitedSet<int, 2>{25, 42};
  if constexpr (!::mbo::config::kRequireThrows) {
    ASSERT_DEATH(kTest.at_index(3), "Out of range");
  } else {
#if __cpp_exceptions
# if !HAS_ADDRESS_SANITIZER
    // Disabled due to https://github.com/google/sanitizers/issues/749
    const bool caught = [&]() {
      try {
        kTest.at_index(3);
      } catch (const std::out_of_range&) {
        return true;
      } catch (...) {
        return false;
      }
      return false;
    }();
    ASSERT_TRUE(caught);
# endif  // !HAS_ADDRESS_SANITIZER
#endif   // __cpp_exceptions
  }
}

// Transparent (heterogeneous) lookup.
//
// A `Key` that counts its own construction, so the tests below can assert the
// POINT of transparent lookup - that no temporary key is materialised - rather
// than merely that the call compiles.
struct CountedString {
  static std::size_t constructions;  // NOLINT(*-non-const-global-variables)

  // EXPLICIT on purpose. An implicit conversion would make `string_view` and
  // `CountedString` `equality_comparable_with` each other via that conversion, which
  // is exactly what transparent lookup exists to avoid needing - and it would let the
  // tests below pass without the transparent machinery doing anything.
  explicit CountedString(std::string_view str) : value(str) { ++constructions; }

  CountedString(const CountedString& other) : value(other.value) { ++constructions; }

  // Defaulted: only the CONSTRUCTORS need to count, and the default handles
  // self-assignment correctly where a hand-written `value = other.value` does not.
  CountedString& operator=(const CountedString&) = default;

  CountedString(CountedString&&) noexcept = default;
  CountedString& operator=(CountedString&&) noexcept = default;
  ~CountedString() = default;

  auto operator<=>(const CountedString& other) const { return value <=> other.value; }

  bool operator==(const CountedString& other) const { return value == other.value; }

  std::string value;
};

std::size_t CountedString::constructions = 0;  // NOLINT(*-non-const-global-variables)

// Orders `CountedString` against anything convertible to `std::string_view`
// without building a `CountedString` to do it.
struct CountedLess {
  using is_transparent = void;

  bool operator()(const CountedString& lhs, const CountedString& rhs) const { return lhs.value < rhs.value; }

  bool operator()(const CountedString& lhs, std::string_view rhs) const { return lhs.value < rhs; }

  bool operator()(std::string_view lhs, const CountedString& rhs) const { return lhs < rhs.value; }

  bool operator()(std::string_view lhs, std::string_view rhs) const { return lhs < rhs; }
};

// Named because the raw type contains commas, which a function-like macro such as
// EXPECT_THAT cannot parse as a single argument.
using CountedSet = LimitedSet<CountedString, LimitedOptions<4>{}, CountedLess>;

// Detects whether a foreign key may be looked up at all. As a named concept the
// substitution failure stays in the immediate context, so it yields false rather
// than a hard error.
template<typename Set, typename K>
concept CanFindWith = requires(const Set& set, const K& key) { set.find(key); };

TEST_F(LimitedSetTest, TransparentLookupFindsWithoutConstructingAKey) {
  CountedSet set;
  set.emplace(CountedString("aaa"));
  set.emplace(CountedString("bbb"));
  set.emplace(CountedString("ccc"));

  const std::size_t before = CountedString::constructions;

  EXPECT_THAT(set.find(std::string_view("bbb")), set.begin() + 1);
  EXPECT_THAT(set.contains(std::string_view("bbb")), true);
  EXPECT_THAT(set.contains(std::string_view("zzz")), false);
  EXPECT_THAT(set.count(std::string_view("ccc")), 1);
  EXPECT_THAT(set.count(std::string_view("zzz")), 0);
  EXPECT_THAT(set.find(std::string_view("zzz")), set.end());

  // The whole point: not one `CountedString` was built to perform those lookups.
  EXPECT_THAT(CountedString::constructions, before) << "transparent lookup constructed a Key";
}

TEST_F(LimitedSetTest, TransparentLookupBoundsAndEqualRange) {
  CountedSet set;
  set.emplace(CountedString("aaa"));
  set.emplace(CountedString("bbb"));
  set.emplace(CountedString("ccc"));

  const std::size_t before = CountedString::constructions;

  EXPECT_THAT(set.lower_bound(std::string_view("bbb")), set.begin() + 1);
  EXPECT_THAT(set.upper_bound(std::string_view("bbb")), set.begin() + 2);
  const auto [first, last] = set.equal_range(std::string_view("bbb"));
  EXPECT_THAT(first, set.begin() + 1);
  EXPECT_THAT(last, set.begin() + 2);
  EXPECT_THAT(set.index_of(std::string_view("ccc")), 2);
  EXPECT_THAT(set.index_of(std::string_view("zzz")), CountedSet::npos);

  EXPECT_THAT(CountedString::constructions, before) << "transparent lookup constructed a Key";
}

TEST_F(LimitedSetTest, NonTransparentComparatorHasNoForeignKeyLookup) {
  // `std::less<int>` is not transparent, so the foreign-key overloads must not
  // exist - otherwise they would silently accept anything convertible to the key.
  // `std::less<int>` is the point of this test - `std::less<>` IS transparent and
  // would make the container under test transparent too, asserting nothing.
  // NOLINTNEXTLINE(modernize-use-transparent-functors)
  using NonTransparent = LimitedSet<int, LimitedOptions<4>{}, std::less<int>>;
  static_assert(!NonTransparent::kTransparent);
  static_assert(!CanFindWith<NonTransparent, const char*>);

  // ... whereas a transparent one does.
  static_assert(CountedSet::kTransparent);
  static_assert(CanFindWith<CountedSet, std::string_view>);
  // An exact `Key` still works on both.
  static_assert(CanFindWith<NonTransparent, int>);
  static_assert(CanFindWith<CountedSet, CountedString>);
}

TEST_F(LimitedSetTest, CountReturnsPresenceForOrdinaryKeys) {
  // `count` never compiled before: it returned `last - false` instead of
  // `last - first`, which no test instantiated.
  const auto set = MakeLimitedSet(1, 2, 3);
  EXPECT_THAT(set.count(2), 1);
  EXPECT_THAT(set.count(9), 0);
}

TEST_F(LimitedSetTest, TransparentEraseConstructsNoKey) {
  CountedSet set;
  set.emplace(CountedString("aaa"));
  set.emplace(CountedString("bbb"));
  set.emplace(CountedString("ccc"));

  const std::size_t before = CountedString::constructions;

  EXPECT_THAT(set.erase(std::string_view("bbb")), 1);
  EXPECT_THAT(set.erase(std::string_view("zzz")), 0) << "absent key erases nothing";
  EXPECT_THAT(set, SizeIs(2));

  // With a transparent comparator `erase` compares the foreign key directly, so it
  // has no reason to build a Key at all.
  EXPECT_THAT(CountedString::constructions, before) << "transparent erase constructed a Key";
}

TEST_F(LimitedSetTest, EraseByExactKeyUsesTheNonTemplateOverload) {
  // A non-const lvalue is the case that regressed: `K&&` deduces `Key&`, which beats
  // `const Key&` at overload resolution unless the template excludes `Key`.
  CountedSet set;
  set.emplace(CountedString("aaa"));
  set.emplace(CountedString("bbb"));

  // Deliberately NOT const: a non-const lvalue is the case under test, since that is
  // what `K&&` deduces as `Key&`. Making it const would exercise a different overload.
  // NOLINTNEXTLINE(misc-const-correctness)
  CountedString key("aaa");
  const std::size_t before = CountedString::constructions;
  EXPECT_THAT(set.erase(key), 1) << "erase by non-const lvalue key";
  EXPECT_THAT(CountedString::constructions, before) << "erase by exact key copied the key";
  EXPECT_THAT(set, SizeIs(1));

  // A temporary of the exact key type must work too, and still not convert.
  EXPECT_THAT(set.erase(CountedString("bbb")), 1);
  EXPECT_THAT(set, IsEmpty());
}

TEST_F(LimitedSetTest, EraseByConvertibleKeyWithoutTransparentComparator) {
  // No `is_transparent`: the foreign key must be converted to a Key exactly once,
  // which is the path the forwarding reference exists for.
  LimitedSet<long, LimitedOptions<4>{}> set;  // NOLINT(google-runtime-int)
  set.emplace(1);
  set.emplace(2);
  set.emplace(3);

  EXPECT_THAT(set.erase(2), 1) << "int erased from a set of long";
  EXPECT_THAT(set, ElementsAre(1, 3));
  EXPECT_THAT(set.erase(9), 0);
  EXPECT_THAT(set, SizeIs(2));
}

TEST_F(LimitedSetTest, TransparentContainsAllAndAny) {
  CountedSet set;
  set.emplace(CountedString("aaa"));
  set.emplace(CountedString("bbb"));
  set.emplace(CountedString("ccc"));

  const std::vector<std::string_view> present{"aaa", "ccc"};
  const std::vector<std::string_view> partial{"aaa", "zzz"};
  const std::vector<std::string_view> absent{"yyy", "zzz"};

  const std::size_t before = CountedString::constructions;

  EXPECT_THAT(set.contains_all(present), true);
  EXPECT_THAT(set.contains_all(partial), false);
  EXPECT_THAT(set.contains_any(partial), true);
  EXPECT_THAT(set.contains_any(absent), false);

  EXPECT_THAT(CountedString::constructions, before) << "contains_all/any constructed a Key";
}

TEST_F(LimitedSetTest, CompareLessIsTransparent) {
  // CompareLess always ordered its value_type against anything three-way comparable
  // with it; it just never said `is_transparent`, so a container could not use those
  // overloads and every lookup had to build a key first.
  using CompareLessSet =
      LimitedSet<long, LimitedOptions<4>{}, mbo::types::CompareLess<long>>;  // NOLINT(google-runtime-int)
  static_assert(CompareLessSet::kTransparent);

  CompareLessSet set;
  set.emplace(1);
  set.emplace(2);
  set.emplace(3);

  // `int` is a foreign key here: the set's key type is `long`.
  EXPECT_THAT(set.contains(2), true);
  EXPECT_THAT(set.count(3), 1);
  EXPECT_THAT(set.find(9), set.end());
  EXPECT_THAT(set.index_of(1), 0);
  EXPECT_THAT(set.erase(2), 1);
  EXPECT_THAT(set, ElementsAre(1, 3));
}

// NOLINTEND(*-magic-numbers)

}  // namespace
}  // namespace mbo::container
