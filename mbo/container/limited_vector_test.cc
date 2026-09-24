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

#include "mbo/container/limited_vector.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <ranges>  // IWYU pragma: keep
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>  // IWYU pragma: keep
#include <utility>
#include <vector>

#include "absl/log/initialize.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"

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
using ::testing::Not;
using ::testing::SizeIs;

static_assert(std::ranges::range<LimitedVector<int, 3>>);
static_assert(std::random_access_iterator<LimitedVector<int, 3>::iterator>);
static_assert(std::random_access_iterator<LimitedVector<int, 3>::const_iterator>);
static_assert(!std::contiguous_iterator<LimitedVector<int, 3>::iterator>);
static_assert(!std::contiguous_iterator<LimitedVector<int, 3>::const_iterator>);
static_assert(sizeof(LimitedVector<std::size_t, 0>) == sizeof(LimitedVector<std::size_t, 1>));
static_assert(sizeof(LimitedVector<std::size_t, 2>) == sizeof(LimitedVector<std::size_t, 1>) + sizeof(std::size_t));

constexpr LimitedVector<std::string, 2> kConstexprStrings{"one", "two"};
static_assert(kConstexprStrings.size() == 2);
static_assert(kConstexprStrings.at(0) == "one");
static_assert(kConstexprStrings.at(1) == "two");

struct LimitedVectorTest : ::testing::Test {
  static void SetUpTestSuite() { absl::InitializeLog(); }
};

struct TrackedValue {
  constexpr TrackedValue(int new_value, int& live_count) noexcept : value(new_value), live_count(&live_count) {
    ++*this->live_count;
  }

  constexpr TrackedValue(const TrackedValue& other) noexcept : value(other.value), live_count(other.live_count) {
    ++*live_count;
  }

  constexpr TrackedValue(TrackedValue&& other) noexcept : value(other.value), live_count(other.live_count) {
    ++*live_count;
  }

  constexpr TrackedValue& operator=(const TrackedValue& other) noexcept {
    if (this != &other) {
      value = other.value;
    }
    return *this;
  }

  constexpr TrackedValue& operator=(TrackedValue&& other) noexcept {
    value = other.value;
    return *this;
  }

  constexpr ~TrackedValue() noexcept { --*live_count; }

  constexpr operator int() const noexcept { return value; }  // NOLINT(*-explicit-*)

  int value;
  int* live_count;
};

struct CopyConstructOnly {
  constexpr explicit CopyConstructOnly(int new_value) noexcept : value(new_value) {}

  constexpr CopyConstructOnly(const CopyConstructOnly&) noexcept = default;
  constexpr CopyConstructOnly(CopyConstructOnly&&) noexcept = default;
  constexpr CopyConstructOnly& operator=(const CopyConstructOnly&) = delete;
  constexpr CopyConstructOnly& operator=(CopyConstructOnly&&) = delete;
  constexpr ~CopyConstructOnly() noexcept = default;

  constexpr operator int() const noexcept { return value; }  // NOLINT(*-explicit-*)

  int value;
};

struct ImmovableTracked final {
  constexpr ImmovableTracked(int new_value, int& live_count) noexcept : value(new_value), live_count(&live_count) {
    ++*this->live_count;
  }

  ImmovableTracked(const ImmovableTracked&) = delete;
  ImmovableTracked& operator=(const ImmovableTracked&) = delete;
  ImmovableTracked(ImmovableTracked&&) = delete;
  ImmovableTracked& operator=(ImmovableTracked&&) = delete;

  constexpr ~ImmovableTracked() noexcept { --*live_count; }

  constexpr operator int() const noexcept { return value; }  // NOLINT(*-explicit-*)

  int value;
  int* live_count;
};

struct Incomplete;

struct ThrowingDestructor final {
  ThrowingDestructor() = default;
  ThrowingDestructor(const ThrowingDestructor&) = default;
  ThrowingDestructor& operator=(const ThrowingDestructor&) = default;
  ThrowingDestructor(ThrowingDestructor&&) = default;
  ThrowingDestructor& operator=(ThrowingDestructor&&) = default;

  ~ThrowingDestructor() noexcept(false) {}  // NOLINT(modernize-use-equals-default)
};

using ImmovableVector = LimitedVector<ImmovableTracked, 3>;

template<typename Vector>
concept HasImmovableCoreOperations = requires(Vector& values, int& live_count) {
  { values.emplace_back(1, live_count) } -> std::same_as<typename Vector::reference>;
  values.pop_back();
  values.clear();
  values[0];
  values.begin();
  values.end();
};

template<typename Vector>
concept HasConstPushBack = requires(Vector& values, const Vector::value_type& value) { values.push_back(value); };

template<typename Vector>
concept HasMovePushBack = requires(Vector& values, Vector::value_type&& value) { values.push_back(std::move(value)); };

template<typename Vector>
concept HasPositionalEmplace =
    requires(Vector& values, int& live_count) { values.emplace(values.begin(), 1, live_count); };

template<typename Vector>
concept HasErase = requires(Vector& values) { values.erase(values.begin()); };

template<typename Vector>
concept HasDefaultResize = requires(Vector& values) { values.resize(1); };

template<typename Vector>
concept HasSwap = requires(Vector& lhs, Vector& rhs) { lhs.swap(rhs); };

static_assert(LimitedVectorValid<ImmovableTracked>);
static_assert(LimitedVectorValid<const int>);
static_assert(!LimitedVectorValid<void>);
static_assert(!LimitedVectorValid<std::remove_reference_t<decltype("x")>>);
static_assert(!LimitedVectorValid<volatile int>);
static_assert(!LimitedVectorValid<Incomplete>);
static_assert(!LimitedVectorValid<ThrowingDestructor>);
static_assert(HasImmovableCoreOperations<ImmovableVector>);
static_assert(!std::copy_constructible<ImmovableVector>);
static_assert(!std::move_constructible<ImmovableVector>);
static_assert(!std::is_copy_assignable_v<ImmovableVector>);
static_assert(!std::is_move_assignable_v<ImmovableVector>);
static_assert(!HasConstPushBack<ImmovableVector>);
static_assert(!HasMovePushBack<ImmovableVector>);
static_assert(!HasPositionalEmplace<ImmovableVector>);
static_assert(!HasErase<ImmovableVector>);
static_assert(!HasDefaultResize<ImmovableVector>);
static_assert(!HasSwap<ImmovableVector>);

template<typename T>
void CopyAssign(T& lhs, const T& rhs) {
  lhs = rhs;
}

template<typename T>
void MoveAssign(T& lhs, T& rhs) {
  lhs = std::move(rhs);
}

TEST_F(LimitedVectorTest, MakeNoArg) {
  constexpr auto kTest = MakeLimitedVector<int>();
  EXPECT_THAT(kTest, IsEmpty());
  EXPECT_THAT(kTest, SizeIs(0));
  EXPECT_THAT(kTest, CapacityIs(0));
  EXPECT_THAT(kTest, ElementsAre());
}

TEST_F(LimitedVectorTest, ZeroCapacityHasAValidEmptyIteratorRange) {
  LimitedVector<int, 0> values;
  EXPECT_THAT(values.begin(), Eq(values.end()));
  EXPECT_THAT(values.cbegin(), Eq(values.cend()));
  values.reserve(0);
  values.clear();
  EXPECT_THAT(values, IsEmpty());
}

TEST_F(LimitedVectorTest, MakeOneArg) {
  constexpr auto kTest = MakeLimitedVector(42);
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(1));
  EXPECT_THAT(kTest, CapacityIs(1));
  EXPECT_THAT(kTest, ElementsAre(42));
}

TEST_F(LimitedVectorTest, MakeInitArg) {
  constexpr auto kTest = MakeLimitedVector<3>({0, 1, 2});
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(3));
  EXPECT_THAT(kTest, CapacityIs(3));
  EXPECT_THAT(kTest, ElementsAre(0, 1, 2));
}

TEST_F(LimitedVectorTest, MakeInitArgLarger) {
  constexpr auto kTest = MakeLimitedVector<5>({0, 1, 2});
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(3));
  EXPECT_THAT(kTest, CapacityIs(5));
  EXPECT_THAT(kTest, ElementsAre(0, 1, 2));
}

TEST_F(LimitedVectorTest, MakeMultiArg) {
  constexpr auto kTest = MakeLimitedVector(0, 1, 2, 3);
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(4));
  EXPECT_THAT(kTest, CapacityIs(4));
  EXPECT_THAT(kTest, ElementsAre(0, 1, 2, 3));
}

TEST_F(LimitedVectorTest, MakeNumArg) {
  // auto kTest0 = MakeLimitedVector<0>(42);
  // EXPECT_THAT(kTst0, IsEmpty());
  // EXPECT_THAT(kTest0, SizeIs(0));
  // EXPECT_THAT(kTest0, ElementsAre());
  constexpr auto kTest1 = MakeLimitedVector<1>(42);
  EXPECT_THAT(kTest1, Not(IsEmpty()));
  EXPECT_THAT(kTest1, SizeIs(1));
  EXPECT_THAT(kTest1, CapacityIs(1));
  EXPECT_THAT(kTest1, ElementsAre(42));
  constexpr auto kTest2 = MakeLimitedVector<2>(42);
  EXPECT_THAT(kTest2, Not(IsEmpty()));
  EXPECT_THAT(kTest2, SizeIs(2));
  EXPECT_THAT(kTest2, CapacityIs(2));
  EXPECT_THAT(kTest2, ElementsAre(42, 42));
}

TEST_F(LimitedVectorTest, MakeIteratorArg) {
  constexpr std::array<int, 4> kVec{0, 1, 2, 3};
  constexpr auto kTest = MakeLimitedVector<5>(kVec.begin(), kVec.end());
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(4));
  EXPECT_THAT(kTest, CapacityIs(5));
  EXPECT_THAT(kTest, ElementsAre(0, 1, 2, 3));
}

TEST_F(LimitedVectorTest, MakeWithStrings) {
  const std::vector<std::string> data{{"0"}, {"1"}, {"2"}, {"3"}};
  auto test = MakeLimitedVector<4>(data.begin(), data.end());
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(4));
  EXPECT_THAT(test, CapacityIs(4));
  EXPECT_THAT(test, ElementsAre("0", "1", "2", "3"));
}

TEST_F(LimitedVectorTest, ConstructAssignFromSmaller) {
  {
    constexpr LimitedVector<unsigned, 3> kSource({0U, 1U, 2U});
    const LimitedVector<int, 5> target(kSource);
    EXPECT_THAT(target, ElementsAre(0, 1, 2));
  }
  {
    constexpr LimitedVector<unsigned, 3> kSource({0U, 1U, 2U});
    LimitedVector<int, 5> target;
    ASSERT_THAT(target, IsEmpty());
    target = kSource;
    EXPECT_THAT(target, ElementsAre(0, 1, 2));
  }
  {
    LimitedVector<unsigned, 4> source({0U, 1U, 2U});
    const LimitedVector<int, 5> target(std::move(source));
    EXPECT_THAT(target, ElementsAre(0, 1, 2));
  }
  {
    LimitedVector<unsigned, 3> source({0U, 1U, 2U});
    LimitedVector<int, 5> target;
    ASSERT_THAT(target, IsEmpty());
    target = std::move(source);
    EXPECT_THAT(target, ElementsAre(0, 1, 2));
  }
}

TEST_F(LimitedVectorTest, ToLimitedVector) {
  // NOLINTBEGIN(*-avoid-c-arrays)
  constexpr int kArray[4] = {0, 1, 2, 3};
  constexpr auto kTest = ToLimitedVector(kArray);
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(4));
  EXPECT_THAT(kTest, CapacityIs(4));
  EXPECT_THAT(kTest, ElementsAre(0, 1, 2, 3));
  constexpr auto kOther = ToLimitedVector({0, 1, 2, 3});
  EXPECT_THAT(kOther, ElementsAre(0, 1, 2, 3));
  constexpr auto kTyped = ToLimitedVector<uint32_t>({0, 1, 2, 3});
  EXPECT_THAT(kTyped, ElementsAre(0, 1, 2, 3));
  // NOLINTEND(*-avoid-c-arrays)
}

TEST_F(LimitedVectorTest, ToLimitedVectorStringCopy) {
  // NOLINTBEGIN(*-avoid-c-arrays)
  const std::string array[4] = {{"0"}, {"1"}, {"2"}, {"3"}};
  auto test = ToLimitedVector(array);
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(4));
  EXPECT_THAT(test, CapacityIs(4));
  EXPECT_THAT(test, ElementsAre("0", "1", "2", "3"));
  // NOLINTEND(*-avoid-c-arrays)
}

TEST_F(LimitedVectorTest, ToLimitedVectorStringMove) {
  // NOLINTBEGIN(*-avoid-c-arrays)
  std::string array[4] = {{"0"}, {"1"}, {"2"}, {"3"}};
  auto test = ToLimitedVector(std::move(array));
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(4));
  EXPECT_THAT(test, CapacityIs(4));
  EXPECT_THAT(test, ElementsAre("0", "1", "2", "3"));
  // NOLINTEND(*-avoid-c-arrays)
}

TEST_F(LimitedVectorTest, ConstexprMakeClear) {
  constexpr auto kTest = [] {
    auto test = MakeLimitedVector<5>({0, 1, 2});
    test.clear();
    return test;
  }();
  EXPECT_THAT(kTest, IsEmpty());
  EXPECT_THAT(kTest, SizeIs(0));
  EXPECT_THAT(kTest, CapacityIs(5));
  EXPECT_THAT(kTest, ElementsAre());
}

TEST_F(LimitedVectorTest, ConstexprMakePushPop) {
  constexpr auto kTest = [] {
    auto test = MakeLimitedVector<6>({0, 1, 2});
    test.pop_back();
    test.emplace_back(3);
    test.push_back(4);
    return test;
  }();
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(4));
  EXPECT_THAT(kTest, CapacityIs(6));
  EXPECT_THAT(kTest, ElementsAre(0, 1, 3, 4));
}

TEST_F(LimitedVectorTest, ReadAccess) {
  constexpr auto kTest = MakeLimitedVector<6>({0, 1, 2, 3});
  EXPECT_THAT(kTest, Not(IsEmpty()));
  EXPECT_THAT(kTest, SizeIs(4));
  EXPECT_THAT(kTest, CapacityIs(6));
  ASSERT_THAT(kTest, ElementsAre(0, 1, 2, 3));
  EXPECT_THAT(kTest.front(), 0);
  EXPECT_THAT(kTest.at(1), 1);
  EXPECT_THAT(kTest.at(2), 2);
  EXPECT_THAT(kTest.back(), 3);
}

TEST_F(LimitedVectorTest, WriteAccess) {
  auto test = MakeLimitedVector<6>({0, 1, 2, 3});
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(4));
  EXPECT_THAT(test, CapacityIs(6));
  ASSERT_THAT(test, ElementsAre(0, 1, 2, 3));
  test.front() = 10;
  test.at(1) = 11;
  test.at(2) = 12;
  test.back() = 13;
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(4));
  EXPECT_THAT(test, CapacityIs(6));
  ASSERT_THAT(test, ElementsAre(10, 11, 12, 13));
}

TEST_F(LimitedVectorTest, Emplace) {
  auto test = MakeLimitedVector<7>({1, 3});
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(2));
  EXPECT_THAT(test, CapacityIs(7));
  ASSERT_THAT(test, ElementsAre(1, 3));
  EXPECT_THAT(test.emplace(test.begin() + 1, 20), test.begin() + 1);
  EXPECT_THAT(test, SizeIs(3));
  EXPECT_THAT(test, ElementsAre(1, 20, 3));
  EXPECT_THAT(test.emplace(test.end(), 40), test.begin() + 3);
  EXPECT_THAT(test, SizeIs(4));
  EXPECT_THAT(test, ElementsAre(1, 20, 3, 40));
  EXPECT_THAT(test.emplace(test.begin(), 0), test.begin());
  EXPECT_THAT(test, SizeIs(5));
  EXPECT_THAT(test, ElementsAre(0, 1, 20, 3, 40));
}

TEST_F(LimitedVectorTest, Erase) {
  auto test = MakeLimitedVector(0, 1, 20, 3, 40);
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(5));
  EXPECT_THAT(test, CapacityIs(5));
  ASSERT_THAT(test, ElementsAre(0, 1, 20, 3, 40));
  EXPECT_THAT(test.erase(test.begin() + 2), test.begin() + 2);
  EXPECT_THAT(test, SizeIs(4));
  EXPECT_THAT(test, ElementsAre(0, 1, 3, 40));
  EXPECT_THAT(test.erase(test.end() - 1), test.begin() + 3);
  EXPECT_THAT(test.begin() + 3, test.end()) << "Should have returned new `end`.";
  EXPECT_THAT(test, SizeIs(3));
  EXPECT_THAT(test, ElementsAre(0, 1, 3));
  EXPECT_THAT(test.erase(test.begin()), test.begin());
  EXPECT_THAT(test, SizeIs(2));
  EXPECT_THAT(test, ElementsAre(1, 3));
  EXPECT_THAT(test.erase(test.begin()), test.begin());
  EXPECT_THAT(test.erase(test.begin()), test.begin());
  EXPECT_THAT(test.begin(), test.end()) << "Should have returned new `end`.";
  EXPECT_THAT(test, IsEmpty());
}

TEST_F(LimitedVectorTest, EraseRange) {
  auto test = MakeLimitedVector(0, 1, 20, 3, 40, 50, 60, 70, 8, 9);
  using Type = decltype(test);
  EXPECT_THAT(test, Not(IsEmpty()));
  EXPECT_THAT(test, SizeIs(10));
  EXPECT_THAT(test, CapacityIs(10));
  ASSERT_THAT(test, ElementsAre(0, 1, 20, 3, 40, 50, 60, 70, 8, 9));
  const Type::const_iterator first = test.begin() + 4;
  const Type::const_iterator last = test.begin() + 8;
  const Type::const_iterator it = test.erase(first, last);
  EXPECT_THAT(it, test.begin() + 4);
  EXPECT_THAT(test, SizeIs(6));
  EXPECT_THAT(test, ElementsAre(0, 1, 20, 3, 8, 9));
  EXPECT_THAT(test.erase(test.begin() + 3, test.end()), test.begin() + 3);
  EXPECT_THAT(test.begin() + 3, test.end()) << "Should have returned new `end`.";
  EXPECT_THAT(test, SizeIs(3));
  EXPECT_THAT(test, ElementsAre(0, 1, 20));
  EXPECT_THAT(test.erase(test.begin(), test.end()), test.begin());
  EXPECT_THAT(test.begin(), test.end()) << "Should have returned new `end`.";
  EXPECT_THAT(test, IsEmpty());
}

TEST_F(LimitedVectorTest, Swap) {
  auto test1 = MakeLimitedVector(0, 1, 2);
  auto test2 = MakeLimitedVector<3>(3);
  test2.pop_back();
  test2.pop_back();
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

TEST_F(LimitedVectorTest, Iterators) {
  constexpr auto kTest = MakeLimitedVector(0, 1, 2);
  // Restrictions apply: The two following cannot be constexpr.
  EXPECT_THAT((MakeLimitedVector<3>(kTest.begin(), kTest.end())), ElementsAre(0, 1, 2));
  EXPECT_THAT((MakeLimitedVector<3>(kTest.rbegin(), kTest.rend())), ElementsAre(2, 1, 0));
}

TEST_F(LimitedVectorTest, IteratorsTraverseSlotsWithoutClaimingContiguousStorage) {
  LimitedVector<int, 4> values{3, 1, 2};
  EXPECT_THAT(values.end() - values.begin(), 3);
  EXPECT_THAT(&*values.begin(), &values.front());
  EXPECT_THAT(&*(values.begin() + 2), &values.back());
  // Intentionally exercise the iterator's checked-by-test random-access operation.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  EXPECT_THAT(values.begin()[1], 1);
  LimitedVector<int, 4> other;
  EXPECT_THAT(values.begin() == other.begin(), false);
  std::ranges::sort(values);
  EXPECT_THAT(values, ElementsAre(1, 2, 3));

  const LimitedVector<int, 0> empty;
  EXPECT_THAT(empty.begin(), empty.end());
  EXPECT_THAT(empty.end() - empty.begin(), 0);

  LimitedVector<const int, 2> immutable;
  immutable.emplace_back(1);
  immutable.emplace_back(2);
  EXPECT_THAT(immutable, ElementsAre(1, 2));
}

TEST_F(LimitedVectorTest, MoveAssignmentAndSelfSwapPreserveValues) {
  LimitedVector<int, 4> source{1, 2};
  LimitedVector<int, 4> target{3};
  target = std::move(source);
  // Verifying the container's documented empty moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_THAT(source, IsEmpty());
  EXPECT_THAT(target, ElementsAre(1, 2));
  target.swap(target);
  EXPECT_THAT(target, ElementsAre(1, 2));
}

TEST_F(LimitedVectorTest, InsertsInitializerList) {
  LimitedVector<int, 5> values{1, 4};
  values.insert(values.begin() + 1, {2, 3});
  EXPECT_THAT(values, ElementsAre(1, 2, 3, 4));
}

TEST_F(LimitedVectorTest, MaintainsObjectLifetimesAcrossCopyMoveSwapInsertAndErase) {
  int live_count = 0;
  {
    LimitedVector<TrackedValue, 6> values;
    values.emplace_back(1, live_count);
    values.emplace_back(2, live_count);
    values.emplace_back(3, live_count);
    EXPECT_THAT(live_count, 3);

    values.emplace(values.begin() + 1, 4, live_count);
    EXPECT_THAT(values, ElementsAre(1, 4, 2, 3));
    EXPECT_THAT(live_count, 4);
    values.erase(values.begin() + 2);
    EXPECT_THAT(values, ElementsAre(1, 4, 3));
    EXPECT_THAT(live_count, 3);

    LimitedVector<TrackedValue, 6> copied(values);
    EXPECT_THAT(live_count, 6);
    copied = values;
    EXPECT_THAT(live_count, 6);
    CopyAssign(copied, copied);
    MoveAssign(copied, copied);
    EXPECT_THAT(copied, ElementsAre(1, 4, 3));
    EXPECT_THAT(live_count, 6);

    LimitedVector<TrackedValue, 6> moved(std::move(copied));
    // Verifying the container's documented empty moved-from state.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_THAT(copied, IsEmpty());
    EXPECT_THAT(moved, ElementsAre(1, 4, 3));
    EXPECT_THAT(live_count, 6);

    LimitedVector<TrackedValue, 6> other;
    other.emplace_back(8, live_count);
    moved.swap(other);
    EXPECT_THAT(moved, ElementsAre(8));
    EXPECT_THAT(other, ElementsAre(1, 4, 3));
    EXPECT_THAT(live_count, 7);
  }
  EXPECT_THAT(live_count, 0);
}

TEST_F(LimitedVectorTest, StagesAliasedValuesAndRangesBeforeInsertionOrAssignment) {
  LimitedVector<std::string, 8> values{"one", "two", "three"};
  values.insert(values.begin() + 1, values.front());
  EXPECT_THAT(values, ElementsAre("one", "one", "two", "three"));

  values.insert(values.begin() + 2, values.begin(), values.begin() + 2);
  EXPECT_THAT(values, ElementsAre("one", "one", "one", "one", "two", "three"));

  values.assign(values.begin() + 1, values.begin() + 4);
  EXPECT_THAT(values, ElementsAre("one", "one", "one"));
}

TEST_F(LimitedVectorTest, SupportsMoveOnlyResourceValues) {
  LimitedVector<std::unique_ptr<int>, 4> values;
  values.emplace_back(std::make_unique<int>(1));
  values.emplace_back(std::make_unique<int>(3));
  values.emplace(values.begin() + 1, std::make_unique<int>(2));
  ASSERT_THAT(values, SizeIs(3));
  EXPECT_THAT(*values.at(0), 1);
  EXPECT_THAT(*values.at(1), 2);
  EXPECT_THAT(*values.at(2), 3);
  values.erase(values.begin());
  ASSERT_THAT(values, SizeIs(2));
  EXPECT_THAT(*values.at(0), 2);
  EXPECT_THAT(*values.at(1), 3);
}

TEST_F(LimitedVectorTest, SupportsImmovableElementLifetimeAndIteration) {
  int live_count = 0;
  {
    ImmovableVector values;
    values.emplace_back(1, live_count);
    values.emplace_back(2, live_count);
    EXPECT_THAT(values, ElementsAre(1, 2));
    EXPECT_THAT(live_count, Eq(2));

    values.pop_back();
    EXPECT_THAT(values, ElementsAre(1));
    EXPECT_THAT(live_count, Eq(1));

    values.clear();
    EXPECT_THAT(values, IsEmpty());
    EXPECT_THAT(live_count, Eq(0));

    values.emplace_back(3, live_count);
    EXPECT_THAT(values.at(0), Eq(3));
    EXPECT_THAT(live_count, Eq(1));
  }
  EXPECT_THAT(live_count, Eq(0));
}

TEST_F(LimitedVectorTest, CopyAssignmentReconstructsNonassignableElements) {
  const LimitedVector<CopyConstructOnly, 3> source{CopyConstructOnly(1), CopyConstructOnly(2)};
  LimitedVector<CopyConstructOnly, 3> target{CopyConstructOnly(3)};
  target = source;
  EXPECT_THAT(target, ElementsAre(1, 2));
}

TEST_F(LimitedVectorTest, RangeInsertionAcceptsSinglePassIterators) {
  LimitedVector<int, 6> values{1, 4};
  std::istringstream input("2 3");
  values.insert(values.begin() + 1, std::istream_iterator<int>(input), std::istream_iterator<int>());
  EXPECT_THAT(values, ElementsAre(1, 2, 3, 4));

  LimitedVector<std::int64_t, 3> assigned;
  assigned = {1, 2, 3};
  EXPECT_THAT(assigned, ElementsAre(1, 2, 3));
}

TEST_F(LimitedVectorTest, Compare) {
  constexpr auto k42v25 = MakeLimitedVector(42, 25);
  constexpr auto k42o25 = MakeLimitedVector(42, 25);
  constexpr auto k42v33 = MakeLimitedVector(42, 33);
  constexpr auto k42 = MakeLimitedVector(42);
  EXPECT_THAT(k42v25 == k42o25, true);
  EXPECT_THAT(k42v25, k42o25);
  EXPECT_THAT(k42v25, Eq(k42o25));

  EXPECT_THAT(k42v25 != k42v33, true);
  EXPECT_THAT(k42v25, Not(k42v33));
  EXPECT_THAT(k42v25, Not(Eq(k42v33)));
  EXPECT_THAT(k42v25 != k42, true);
  EXPECT_THAT(k42v25, Not(k42));
  EXPECT_THAT(k42v25, Not(Eq(k42)));

  EXPECT_THAT(k42v25 < k42v33, true);
  EXPECT_THAT(k42v25, Lt(k42v33));
  EXPECT_THAT(k42 < k42v33, true);
  EXPECT_THAT(k42, Lt(k42v33));
  EXPECT_THAT(k42v33 < k42v25, false);
  EXPECT_THAT(k42v33, Not(Lt(k42v25)));
  EXPECT_THAT(k42v33 < k42, false);
  EXPECT_THAT(k42v33, Not(Lt(k42)));

  EXPECT_THAT(k42v25 <= k42v25, true);
  EXPECT_THAT(k42v25, Le(k42v25));
  EXPECT_THAT(k42v25 <= k42v33, true);
  EXPECT_THAT(k42v25, Le(k42v33));
  EXPECT_THAT(k42 <= k42v33, true);
  EXPECT_THAT(k42, Le(k42v33));

  EXPECT_THAT(k42v33 > k42v25, true);
  EXPECT_THAT(k42v33, Gt(k42v25));

  EXPECT_THAT(k42v25 >= k42, true);
  EXPECT_THAT(k42v25, Ge(k42));
}

TEST_F(LimitedVectorTest, CompareAcrossCapacitySpellings) {
  // The comparison operators used to be declared `template<std::size_t LN, ...>`
  // while the class takes `template<typename T, auto CapacityOrOptions>`, so any
  // instance NOT spelled with a `std::size_t` literal - a `LimitedOptions` value
  // from MakeLimitedVector, or a plain `int` literal - matched no operator at all
  // and failed to compile. `auto` capacity parameters match the class.
  constexpr auto kOpts = MakeLimitedVector<int, 4>();  // LimitedOptions
  constexpr auto kOptsSame = MakeLimitedVector<int, 4>();
  EXPECT_THAT(kOpts == kOptsSame, true);
  EXPECT_THAT((kOpts <=> kOptsSame) == 0, true);
  EXPECT_THAT(kOpts < kOptsSame, false);

  const LimitedVector<int, 5> int_literal{1, 2};  // `int`, not size_t
  const LimitedVector<int, 5UL> size_literal{1, 2};
  EXPECT_THAT(int_literal == size_literal, true);
  EXPECT_THAT(int_literal < size_literal, false);

  // Mixed spellings must compare too.
  EXPECT_THAT(kOpts == int_literal, false);
  EXPECT_THAT(kOpts < int_literal, true);
}

TEST_F(LimitedVectorTest, ComparePartiallyFilled) {
  // The comparison loops used to run to min(capacity, capacity) instead of
  // min(size, size), reading uninitialized slots (or throwing under a
  // require-throws build) whenever the vectors were not full.
  const LimitedVector<int, 5UL> lhs{1, 2};
  const LimitedVector<int, 5UL> rhs{1, 2};
  EXPECT_THAT(lhs == rhs, true);
  EXPECT_THAT((lhs <=> rhs) == 0, true);
  EXPECT_THAT(lhs < rhs, false);
  const LimitedVector<int, 7UL> shorter{1};
  EXPECT_THAT(shorter < lhs, true);
  EXPECT_THAT(lhs == shorter, false);
}

TEST_F(LimitedVectorTest, CompareDifferentType) {
  const auto k42v25 = MakeLimitedVector<std::string>("42", "25");
  constexpr auto k42o25 = MakeLimitedVector<std::string_view>("42", "25");
  constexpr auto k42v33 = MakeLimitedVector("42", "33");
  constexpr auto k42 = MakeLimitedVector("42");
  const auto runtime_string_views = MakeLimitedVector("42", "33");
  EXPECT_THAT(runtime_string_views, ElementsAre("42", "33"));
  EXPECT_THAT(k42v25 == k42o25, true);
  EXPECT_THAT(k42v25, k42o25);
  EXPECT_THAT(k42v25, Eq(k42o25));

  EXPECT_THAT(k42v25 != k42v33, true);
  EXPECT_THAT(k42v25, Not(k42v33));
  EXPECT_THAT(k42v25, Not(Eq(k42v33)));
  EXPECT_THAT(k42v25 != k42, true);
  EXPECT_THAT(k42v25, Not(k42));
  EXPECT_THAT(k42v25, Not(Eq(k42)));

  EXPECT_THAT(k42v25 < k42v33, true);
  EXPECT_THAT(k42v25, Lt(k42v33));
  EXPECT_THAT(k42 < k42v33, true);
  EXPECT_THAT(k42, Lt(k42v33));
  EXPECT_THAT(k42v33 < k42v25, false);
  EXPECT_THAT(k42v33, Not(Lt(k42v25)));
  EXPECT_THAT(k42v33 < k42, false);
  EXPECT_THAT(k42v33, Not(Lt(k42)));

  EXPECT_THAT(k42v25 <= k42v25, true);
  EXPECT_THAT(k42v25, Le(k42v25));
  EXPECT_THAT(k42v25 <= k42v33, true);
  EXPECT_THAT(k42v25, Le(k42v33));
  EXPECT_THAT(k42 <= k42v33, true);
  EXPECT_THAT(k42, Le(k42v33));

  EXPECT_THAT(k42v33 > k42v25, true);
  EXPECT_THAT(k42v33, Gt(k42v25));

  EXPECT_THAT(k42v25 >= k42, true);
  EXPECT_THAT(k42v25, Ge(k42));
}

TEST_F(LimitedVectorTest, ToLimitedVectorEmptyDtor) {
  // Just makes sure this actually can compile.
  static constexpr auto kData = ToLimitedVector<int, LimitedOptionsFlag::kEmptyDestructor>({3, 5});
  EXPECT_THAT(kData, ElementsAre(3, 5));
}

// Test struct that increases a value when the destructor is called.
// In copy and move we decrease the value as there will be two destructor calls.
struct IncOnDtor {
  constexpr ~IncOnDtor() noexcept { ++(*dtor_called); }

  constexpr explicit IncOnDtor(int* dtor_called) noexcept : dtor_called(dtor_called) {}

  constexpr IncOnDtor(const IncOnDtor& other) noexcept : dtor_called(other.dtor_called) { --*dtor_called; }

  constexpr IncOnDtor& operator=(const IncOnDtor& other) noexcept {
    if (this != &other) {
      dtor_called = other.dtor_called;
      --*dtor_called;
    }
    return *this;
  }

  constexpr IncOnDtor(IncOnDtor&& other) noexcept : dtor_called(other.dtor_called) { --*other.dtor_called; }

  constexpr IncOnDtor& operator=(IncOnDtor&& other) noexcept = delete;

  constexpr operator int() const noexcept { return *dtor_called; }  // NOLINT(*-explicit-*)

  int* dtor_called;
};

TEST_F(LimitedVectorTest, DtorCheck) {
  int var3 = 3;
  int var5 = 5;
  {
    const LimitedVector<IncOnDtor, LimitedOptions<2, LimitedOptionsFlag::kEmptyDestructor>{}> data{
        IncOnDtor(&var3), IncOnDtor(&var5)};
    EXPECT_THAT(data, ElementsAre(3, 5));
  }
  EXPECT_THAT(var3, 3) << "If this is 4, then LimitedVector called the destructors for its values.";
  EXPECT_THAT(var5, 5) << "If this is 6, then LimitedVector called the destructors for its values.";
  {
    const LimitedVector<IncOnDtor, 2> data{IncOnDtor(&var3), IncOnDtor(&var5)};
    EXPECT_THAT(data, ElementsAre(3, 5));
  }
  EXPECT_THAT(var3, 4) << "Destructor should have been called.";
  EXPECT_THAT(var5, 6) << "Destructor should have been called.";
}

struct BadDtor {
  constexpr ~BadDtor() noexcept = default;

  constexpr explicit BadDtor(int v) noexcept : v(v) {}

  constexpr BadDtor(const BadDtor&) noexcept = default;
  constexpr BadDtor& operator=(const BadDtor&) noexcept = default;
  constexpr BadDtor(BadDtor&&) noexcept = default;
  constexpr BadDtor& operator=(BadDtor&&) noexcept = default;

  constexpr operator int() const noexcept { return v; }  // NOLINT(*-explicit-*)

  int v;
};

TEST_F(LimitedVectorTest, EmptyDtorCheck) {
  static constexpr LimitedVector<BadDtor, LimitedOptions<2, LimitedOptionsFlag::kEmptyDestructor>{}> kData{
      BadDtor(3), BadDtor(5)};
  EXPECT_THAT(kData, ElementsAre(3, 5));
}

TEST_F(LimitedVectorTest, ToLimitedVectorEmptyDtorCheck) {
  static constexpr auto kData =
      ToLimitedVector<BadDtor, LimitedOptionsFlag::kEmptyDestructor>({BadDtor(3), BadDtor(5)});
  EXPECT_THAT(kData, ElementsAre(3, 5));
}

TEST_F(LimitedVectorTest, Assign1) {
  static constexpr auto kData = [] {
    LimitedVector<int, 4> result({1, 2, 3, 4});
    result.assign(2, 3);
    return result;
  }();
  EXPECT_THAT(kData, ElementsAre(3, 3));
}

TEST_F(LimitedVectorTest, Assign2) {
  static constexpr auto kData = [] {
    LimitedVector<int, 4> result({1, 2, 3, 4});
    result.assign({5, 6});
    return result;
  }();
  EXPECT_THAT(kData, ElementsAre(5, 6));
}

TEST_F(LimitedVectorTest, Assign3) {
  static constexpr auto kData = [] {
    LimitedVector<int, 4> result({1, 2, 3, 4});
    result.assign({5, 6});
    return result;
  }();
  EXPECT_THAT(kData, ElementsAre(5, 6));
}

TEST_F(LimitedVectorTest, Insert1WithoutMoving) {
  {
    static constexpr auto kData = [] {
      LimitedVector<int, 5> result{};
      result.insert(result.begin(), 1);
      return result;
    }();
    EXPECT_THAT(kData, ElementsAre(1));
  }
  {
    static constexpr auto kData = [] {
      LimitedVector<int, 5> result{};
      result.insert(result.end(), 1);
      result.insert(result.end(), 2);
      result.insert(result.end(), 3);
      return result;
    }();
    EXPECT_THAT(kData, ElementsAre(1, 2, 3));
  }
}

TEST_F(LimitedVectorTest, Insert1Moving) {
  {
    static constexpr auto kData = [] {
      LimitedVector<int, 5> result{};
      result.insert(result.begin(), 1);
      result.insert(result.begin(), 2);
      result.insert(result.begin(), 3);
      return result;
    }();
    EXPECT_THAT(kData, ElementsAre(3, 2, 1));
  }
  {
    static constexpr auto kData = [] {
      LimitedVector<int, 5> result({1, 2});
      result.insert(result.begin(), 25);
      result.insert(result.begin() + 2, 33);
      result.insert(result.end(), 42);
      return result;
    }();
    EXPECT_THAT(kData, ElementsAre(25, 1, 33, 2, 42));
  }
}

TEST_F(LimitedVectorTest, Insert1ComplexType) {
  // The test uses the nontrivial type `std::string` to verify that insertion moves
  // live elements without constructing over them or leaking their resources.
  // Each test string uses an identification char (e.g. `1`), 16 dots and a comma in order to force memory allocation.
  static constexpr std::string_view kStr1 = "1................,";
  static constexpr std::string_view kStr2 = "2................,";
  static constexpr std::string_view kStr3 = "3................,";
  static constexpr std::string_view kStrA = "A................,";
  static constexpr std::string_view kStrB = "B................,";
  static constexpr std::string_view kStrC = "C................,";
  static constexpr std::string_view kStrD = "D................,";
  {
    static const auto kData = [] {
      LimitedVector<std::string, 3> result{};
      result.insert(result.begin(), kStr1);
      result.insert(result.begin(), kStr2);
      result.insert(result.begin(), kStr3);
      return result;
    }();
    EXPECT_THAT(kData, ElementsAre(kStr3, kStr2, kStr1));
  }
  {
    static const auto kData = [] {
      LimitedVector<std::string, 6> result(std::initializer_list<std::string_view>{kStr1, kStr2});
      result.insert(result.begin(), kStrA);
      result.insert(result.begin() + 2, kStrB);
      result.insert(result.end(), std::initializer_list<std::string_view>{kStrC, kStrD});
      return result;
    }();
    EXPECT_THAT(kData, ElementsAre(kStrA, kStr1, kStrB, kStr2, kStrC, kStrD));
  }
}

TEST_F(LimitedVectorTest, Insert2) {
  {
    static constexpr auto kData = [] {
      LimitedVector<int, 10> result{};
      result.insert(result.begin(), 1, 0);
      result.insert(result.begin(), 2, 1);
      result.insert(result.begin(), 3, 2);
      result.insert(result.begin(), 4, 3);
      result.insert(result.begin(), 0, 4);
      return result;
    }();
    EXPECT_THAT(kData, ElementsAre(3, 3, 3, 3, 2, 2, 2, 1, 1, 0));
  }
  {
    static constexpr auto kData = [] {
      LimitedVector<int, 10> result{};
      result.insert(result.end(), 1, 0);
      result.insert(result.end(), 2, 1);
      result.insert(result.end(), 3, 2);
      result.insert(result.end(), 4, 3);
      result.insert(result.end(), 0, 4);
      return result;
    }();
    EXPECT_THAT(kData, ElementsAre(0, 1, 1, 2, 2, 2, 3, 3, 3, 3));
  }
  {
    static constexpr auto kData = [] {
      LimitedVector<int, 10> result({1, 2});
      result.insert(result.begin(), 2, 25);
      result.insert(result.begin() + 3, 3, 33);
      result.insert(result.end(), 3, 42);
      return result;
    }();
    EXPECT_THAT(kData, ElementsAre(25, 25, 1, 33, 33, 33, 2, 42, 42, 42));
  }
}

TEST_F(LimitedVectorTest, Insert3) {
  {
    LimitedVector<int, 3> result{1, 2};
    const std::array<int, 0> empty{};
    EXPECT_THAT(result.insert(result.begin() + 1, empty.begin(), empty.end()), result.begin() + 1);
    EXPECT_THAT(result, ElementsAre(1, 2));
  }
  {
    static constexpr auto kData = [] {
      LimitedVector<int, 6> result{};
      result.insert(result.begin(), std::initializer_list<int>{11});
      result.insert(result.begin(), {21, 22});
      result.insert(result.begin(), {31, 32, 33});
      result.insert(result.begin(), std::initializer_list<int>{});
      return result;
    }();
    EXPECT_THAT(kData, ElementsAre(31, 32, 33, 21, 22, 11));
  }
  {
    static constexpr auto kData = [] {
      LimitedVector<int, 6> result{};
      result.insert(result.end(), std::initializer_list<int>{11});
      result.insert(result.end(), {21, 22});
      result.insert(result.end(), {31, 32, 33});
      result.insert(result.end(), std::initializer_list<int>{});
      return result;
    }();
    EXPECT_THAT(kData, ElementsAre(11, 21, 22, 31, 32, 33));
  }
  {
    static constexpr auto kData = [] {
      LimitedVector<int, 8> result({1, 2});
      result.insert(result.begin(), {21, 22});
      result.insert(result.begin() + 3, {31, 32});
      result.insert(result.end(), {41, 42});
      return result;
    }();
    EXPECT_THAT(kData, ElementsAre(21, 22, 1, 31, 32, 2, 41, 42));
  }
}

TEST_F(LimitedVectorTest, RuntimeConvenienceOperations) {
  LimitedVector<int, 4> assigned{1, 2, 3, 4};
  const std::initializer_list<int> replacement{5, 6};
  assigned.assign(replacement);
  EXPECT_THAT(assigned, ElementsAre(5, 6));

  LimitedVector<int, 10> inserted{1, 4};
  const std::array middle{2, 3};
  inserted.insert(inserted.begin() + 1, middle.begin(), middle.end());
  inserted.insert(inserted.end(), 2, 5);
  inserted.insert(inserted.begin(), 0, 9);
  EXPECT_THAT(inserted, ElementsAre(1, 2, 3, 4, 5, 5));

  LimitedVector<int, LimitedOptions<6>{}> options_vector;
  options_vector.push_back(7);
  EXPECT_THAT(options_vector, ElementsAre(7));
  EXPECT_THAT(options_vector.capacity(), 6);

  const auto empty = MakeLimitedVector<int, 7>();
  EXPECT_THAT(empty, IsEmpty());
}

// NOLINTEND(*-magic-numbers)

}  // namespace
}  // namespace mbo::container
