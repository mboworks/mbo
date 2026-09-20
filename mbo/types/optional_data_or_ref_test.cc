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

#include "mbo/types/optional_data_or_ref.h"

#include <compare>
#include <concepts>  // IWYU pragma: keep
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/hash/hash.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"

// NOLINTBEGIN(*-magic-numbers)

namespace mbo::types {
namespace {

using ::mbo::testing::IsNullopt;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;

struct OptionalDataOrRefTest : ::testing::Test {};

struct NothrowValue {
  explicit NothrowValue(int new_value) noexcept : value(new_value) {}

  int value;
};

template<typename T, typename RefT>
void MoveAssign(OptionalDataOrRef<T, RefT>& lhs, OptionalDataOrRef<T, RefT>& rhs) {
  lhs = std::move(rhs);
}

static_assert(noexcept(std::declval<OptionalDataOrRef<NothrowValue>&>().emplace(1)));
static_assert(noexcept(OptionalDataOrRef<NothrowValue>(NothrowValue{1})));

TEST_F(OptionalDataOrRefTest, Constexpr) {
  {
    // NOLINTNEXTLINE(misc-const-correctness): const would change decltype() in the static_assert below.
    constexpr OptionalDataOrRef<int> kRef;
    static_assert(IsOptionalDataOrRef<std::remove_const_t<decltype(kRef)>>);
    EXPECT_THAT(kRef, std::nullopt);
    EXPECT_THAT(kRef, IsNullopt());
    EXPECT_THAT(kRef.has_value(), false);
    EXPECT_THAT(kRef.HoldsData(), false);
    EXPECT_THAT(kRef.HoldsNullopt(), true);
    EXPECT_THAT(kRef.HoldsReference(), false);
    static_assert(kRef.get() == 0);
  }
  {
    static constexpr std::string_view kStr = "test";
    constexpr OptionalDataOrConstRef<std::string_view> kRef{kStr};
    static_assert(IsOptionalDataOrRef<std::remove_const_t<decltype(kRef)>>);
    EXPECT_THAT(kRef, "test");
    EXPECT_THAT(kRef, Not(IsNullopt()));
    EXPECT_THAT(kRef.has_value(), true);
    EXPECT_THAT(kRef.HoldsData(), false);
    EXPECT_THAT(kRef.HoldsNullopt(), false);
    EXPECT_THAT(kRef.HoldsReference(), true);
  }
}

TEST_F(OptionalDataOrRefTest, InitNone) {
  // NOLINTNEXTLINE(misc-const-correctness): the test exercises the mutable type; const changes what is under test.
  OptionalDataOrRef<int> ref;
  EXPECT_THAT(ref, std::nullopt);
  EXPECT_THAT(ref, IsNullopt());
  EXPECT_THAT(ref.has_value(), false);
  EXPECT_THAT(ref.HoldsData(), false);
  EXPECT_THAT(ref.HoldsNullopt(), true);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref.get(), 0);
}

TEST_F(OptionalDataOrRefTest, InitNullopt) {
  // NOLINTNEXTLINE(misc-const-correctness): the test exercises the mutable type; const changes what is under test.
  OptionalDataOrRef<int> ref(std::nullopt);
  EXPECT_THAT(ref, std::nullopt);
  EXPECT_THAT(ref, IsNullopt());
  EXPECT_THAT(ref.has_value(), false);
  EXPECT_THAT(ref.HoldsData(), false);
  EXPECT_THAT(ref.HoldsNullopt(), true);
  EXPECT_THAT(ref.HoldsReference(), false);
}

TEST_F(OptionalDataOrRefTest, InitVal) {
  // NOLINTNEXTLINE(misc-const-correctness): the test exercises the mutable type; const changes what is under test.
  OptionalDataOrRef<int> ref(42);
  EXPECT_THAT(ref, Eq(42));
  EXPECT_THAT(ref, Not(IsNullopt()));
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref.get(), 42);
}

TEST_F(OptionalDataOrRefTest, InitRef) {
  int val = 42;
  // NOLINTNEXTLINE(misc-const-correctness): the test exercises the mutable type; const changes what is under test.
  OptionalDataOrRef<int> ref(val);
  EXPECT_THAT(ref, Eq(42));
  EXPECT_THAT(ref, Not(IsNullopt()));
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), false);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), true);
}

TEST_F(OptionalDataOrRefTest, Value) {
  int val = 10;
  static_assert(std::same_as<int, OptionalDataOrRef<int>::value_type>);
  // NOLINTNEXTLINE(misc-const-correctness): the test exercises the mutable type; const changes what is under test.
  OptionalDataOrRef<int> ref(val);
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), false);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), true);
  EXPECT_THAT(ref, Not(IsNullopt()));
  EXPECT_THAT(*ref, 10);
  val = 11;
  EXPECT_THAT(ref, Eq(11));
  EXPECT_THAT(ref, Eq(11));
  ref = 12;
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref, Eq(12));
  EXPECT_THAT(val, Eq(11)) << "We set an actual value, so val should nto change.";
  ref.set_ref(val);
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), false);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), true);
  EXPECT_THAT(ref, Eq(11));
  EXPECT_THAT(val, Eq(11));
  val = 13;
  EXPECT_THAT(ref, Eq(13));
  EXPECT_THAT(val, Eq(13));
  ref = val;
  val = 14;
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), false);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), true);
  EXPECT_THAT(ref, Eq(14)) << "We were binding to a reference, so setting val updates it.";
  EXPECT_THAT(val, Eq(14));
  int other = 15;
  ref.set_ref(other);
  EXPECT_THAT(ref, Eq(15));
  EXPECT_THAT(val, Eq(14)) << "We did not change this.";
  ref.set_ref(val);
  EXPECT_THAT(ref, Eq(14));
  EXPECT_THAT(val, Eq(14));
  ref.reset();
  EXPECT_THAT(ref.has_value(), false);
  EXPECT_THAT(ref.HoldsData(), false);
  EXPECT_THAT(ref.HoldsNullopt(), true);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref, IsNullopt());
  ref = 15;
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref, Not(IsNullopt()));
  EXPECT_THAT(ref, Not(std::nullopt));
  EXPECT_THAT(ref, Eq(15));
  EXPECT_THAT(val, Eq(14));
  ref.reset();
  EXPECT_THAT(ref.has_value(), false);
  EXPECT_THAT(ref.HoldsData(), false);
  EXPECT_THAT(ref.HoldsNullopt(), true);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref, IsNullopt());
  EXPECT_THAT(ref, std::nullopt);
  val = 16;
  ref.set_ref(val);
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), false);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), true);
  EXPECT_THAT(ref, Not(IsNullopt()));
  EXPECT_THAT(ref, Not(std::nullopt));
  EXPECT_THAT(ref, Eq(16));
  EXPECT_THAT(val, Eq(16));
  val = 17;
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), false);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), true);
  EXPECT_THAT(ref, Eq(17));
  EXPECT_THAT(val, Eq(17));
  EXPECT_THAT(ref.as_data(), 17);
  ref.as_data() = 18;
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref, Eq(18));
  EXPECT_THAT(val, Eq(17));
  ref.emplace(19);
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref, Eq(19));
  EXPECT_THAT(val, Eq(17));
}

TEST_F(OptionalDataOrRefTest, DifferentType) {
  // NOLINTNEXTLINE(misc-const-correctness): the test exercises the mutable type; const changes what is under test.
  OptionalDataOrRef<std::string> ref;
  EXPECT_THAT(ref.has_value(), false);
  EXPECT_THAT(ref.HoldsData(), false);
  EXPECT_THAT(ref.HoldsNullopt(), true);
  EXPECT_THAT(ref.HoldsReference(), false);
  ref = "100";
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref, "100");
  ref = std::string("200");
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref, "200");
  ref = std::string_view("300");
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref, "300");
  const std::string cstr{"400"};
  ref = cstr;
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref, "400");
  const std::string str{"500"};
  ref = str;
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref, "500");
}

TEST_F(OptionalDataOrRefTest, Compare) {
  std::set<OptionalDataOrRef<int>> refs;
  int v25 = 25;
  refs.emplace(v25);
  int v33 = 33;
  refs.emplace(v33);
  refs.emplace(std::nullopt);
  EXPECT_THAT(refs, Contains(IsNullopt()));
  EXPECT_THAT(refs, ElementsAre(IsNullopt(), Eq(25), Eq(33)));
  EXPECT_THAT(refs, Contains(std::nullopt));
  EXPECT_THAT(refs, ElementsAre(std::nullopt, Eq(25), Eq(33)));
}

TEST_F(OptionalDataOrRefTest, ComparisonDistinguishesPresenceAndValue) {
  const OptionalDataOrRef<int> empty;
  const OptionalDataOrRef<int> also_empty;
  const OptionalDataOrRef<int> one(1);
  const OptionalDataOrRef<int> same(1);
  const OptionalDataOrRef<int> two(2);

  EXPECT_THAT(empty == also_empty, IsTrue());
  EXPECT_THAT(empty == one, IsFalse());
  EXPECT_THAT(one == same, IsTrue());
  EXPECT_THAT(one == two, IsFalse());
  EXPECT_THAT(empty <=> also_empty, Eq(std::strong_ordering::equal));
  EXPECT_THAT(empty <=> one, Eq(std::strong_ordering::less));
  EXPECT_THAT(one <=> two, Eq(std::strong_ordering::less));
  EXPECT_THAT(empty == 1, IsFalse());
}

TEST_F(OptionalDataOrRefTest, ConsRef) {
  // NOLINTNEXTLINE(misc-const-correctness): the test exercises the mutable type; const changes what is under test.
  OptionalDataOrConstRef<int> ref(42);
  static_assert(IsOptionalDataOrRef<decltype(ref)>);
  EXPECT_THAT(ref, Eq(42));
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), false);
}

struct TestAsData {
  std::string one = "One";

  template<typename Sink>
  friend constexpr void AbslStringify(Sink& sink, const TestAsData& v) {
    absl::Format(&sink, "%s", v.one);
  }
};

TEST_F(OptionalDataOrRefTest, AsData) {
  // NOLINTNEXTLINE(misc-const-correctness): the test exercises the mutable type; const changes what is under test.
  OptionalDataOrRef<TestAsData> ref;
  EXPECT_THAT(ref.has_value(), false);
  EXPECT_THAT(ref.HoldsData(), false);
  EXPECT_THAT(ref.HoldsNullopt(), true);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref, IsNullopt());
  EXPECT_THAT(ref.as_data().one, "One");
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref.value().one, "One");
  ref.reset();
  EXPECT_THAT(ref.has_value(), false);
  EXPECT_THAT(ref.HoldsData(), false);
  EXPECT_THAT(ref.HoldsNullopt(), true);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref, IsNullopt());
  EXPECT_THAT(ref.as_data("Two").one, "Two");
  EXPECT_THAT(ref.has_value(), true);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref.HoldsNullopt(), false);
  EXPECT_THAT(ref.HoldsReference(), false);
  EXPECT_THAT(ref.value().one, "Two");
}

TEST_F(OptionalDataOrRefTest, CopyAndMovePreserveStateSemantics) {
  OptionalDataOrRef<std::string> owned(std::string("owned"));
  const OptionalDataOrRef<std::string> owned_copy(owned);
  EXPECT_THAT(owned_copy.HoldsData(), true);
  EXPECT_THAT(owned_copy, "owned");

  const OptionalDataOrRef<std::string> moved(std::move(owned));
  EXPECT_THAT(moved.HoldsData(), true);
  EXPECT_THAT(moved, "owned");
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move): moved-from state is under test.
  EXPECT_THAT(owned, IsNullopt());

  OptionalDataOrRef<std::string> assigned;
  assigned = owned_copy;
  EXPECT_THAT(assigned.HoldsData(), true);
  OptionalDataOrRef<std::string> move_assigned;
  move_assigned = std::move(assigned);
  EXPECT_THAT(move_assigned, "owned");
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move): moved-from state is under test.
  EXPECT_THAT(assigned, IsNullopt());

  std::string borrowed = "borrowed";
  OptionalDataOrRef<std::string> referenced(borrowed);
  const OptionalDataOrRef<std::string> reference_copy(referenced);
  const OptionalDataOrRef<std::string> reference_move(std::move(referenced));
  EXPECT_THAT(reference_copy.HoldsReference(), true);
  EXPECT_THAT(reference_move.HoldsReference(), true);
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move): borrowed move preserves the source.
  EXPECT_THAT(referenced.HoldsReference(), true);
  borrowed = "changed";
  EXPECT_THAT(reference_copy, "changed");
  EXPECT_THAT(reference_move, "changed");

  OptionalDataOrRef<std::string> reference_assigned;
  reference_assigned = reference_copy;
  OptionalDataOrRef<std::string> reference_move_assigned;
  reference_move_assigned = std::move(reference_assigned);
  EXPECT_THAT(reference_move_assigned.HoldsReference(), true);
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move): borrowed move preserves the source.
  EXPECT_THAT(reference_assigned.HoldsReference(), true);
}

TEST_F(OptionalDataOrRefTest, SelfMoveAndOwnedValueAliasingPreserveData) {
  OptionalDataOrRef<std::string> ref(std::string("value"));
  MoveAssign(ref, ref);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref, "value");

  ref = std::move(*ref);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref, "value");
}

TEST_F(OptionalDataOrRefTest, NulloptAssignmentReturnsSelf) {
  OptionalDataOrRef<int> ref(42);
  OptionalDataOrRef<int>& result = (ref = std::nullopt);
  EXPECT_THAT(std::addressof(result), Eq(std::addressof(ref)));
  EXPECT_THAT(ref.HoldsNullopt(), true);
}

TEST_F(OptionalDataOrRefTest, StringificationAndHashingReflectPresenceAndValue) {
  const OptionalDataOrRef<int> empty;
  const OptionalDataOrRef<int> value(42);
  const OptionalDataOrRef<int> same(42);

  EXPECT_THAT(absl::StrCat(empty), "std::nullopt");
  EXPECT_THAT(absl::StrCat(value), "42");
  EXPECT_THAT(absl::HashOf(empty) == absl::HashOf(value), IsFalse());
  EXPECT_THAT(absl::HashOf(value), absl::HashOf(same));
}

// NOLINTEND(*-magic-numbers)

}  // namespace
}  // namespace mbo::types
