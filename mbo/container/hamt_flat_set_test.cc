// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/hamt_flat_set.h"

#include <cstdint>
#include <utility>
#include <variant>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container {
namespace {
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::NotNull;
using ::testing::UnorderedElementsAre;
using ::testing::VariantWith;
using Set = HamtFlatSet<int>;

struct HamtFlatSetTest : ::testing::Test {};

struct CollisionHash final {
  std::uint64_t operator()(std::int64_t) const noexcept { return seed; }

  std::uint64_t seed = 19;
};

TEST_F(HamtFlatSetTest, CollisionSnapshotsPreserveStateAndSupportHeterogeneousLookup) {
  using CollisionSet = HamtFlatSet<int, CollisionHash>;
  const CollisionSet empty(CollisionHash{.seed = 29});
  auto edit = empty.transient();
  EXPECT_THAT(edit.insert(1).second, Eq(true));
  EXPECT_THAT(edit.insert(2).second, Eq(true));
  EXPECT_THAT(edit.insert(3).second, Eq(true));
  EXPECT_THAT(edit.hash_function().seed, Eq(29));
  EXPECT_THAT(edit.count(std::int64_t{2}), Eq(1));
  EXPECT_THAT(edit.count(std::int64_t{4}), Eq(0));
  EXPECT_THAT(*edit.find(std::int64_t{2}), Eq(2));
  auto snapshot = std::move(edit).persistent();
  auto [next, erased] = snapshot.erase(std::int64_t{2});
  EXPECT_THAT(erased, Eq(true));
  EXPECT_THAT(next, UnorderedElementsAre(1, 3));
  EXPECT_THAT(snapshot, UnorderedElementsAre(1, 2, 3));
  EXPECT_THAT(next.hash_function().seed, Eq(29));
  EXPECT_THAT(next.count(std::int64_t{2}), Eq(0));
  EXPECT_THAT(next.count(std::int64_t{1}), Eq(1));
  EXPECT_THAT(next.cbegin() == next.begin(), Eq(true));
  EXPECT_THAT(next.cend() == next.end(), Eq(true));
}

TEST_F(HamtFlatSetTest, OrdinaryOperationsMatchTheDocumentedExample) {
  const Set empty;
  auto [one, inserted] = empty.insert(1);
  EXPECT_THAT(inserted, Eq(true));
  auto edit = one.transient();
  auto [position, added] = edit.insert(2);
  EXPECT_THAT(added, Eq(true));
  EXPECT_THAT(*position, Eq(2));
  auto two = std::move(edit).persistent();
  EXPECT_THAT(empty, IsEmpty());
  EXPECT_THAT(one, UnorderedElementsAre(1));
  EXPECT_THAT(two, UnorderedElementsAre(1, 2));
  auto [removed, changed] = two.erase(1);
  EXPECT_THAT(changed, Eq(true));
  EXPECT_THAT(removed, UnorderedElementsAre(2));
  EXPECT_THAT(two, UnorderedElementsAre(1, 2));
  EXPECT_THAT(edit.insert(3).second, Eq(true));
  EXPECT_THAT(edit.erase(3), Eq(1));
  EXPECT_THAT(edit.erase(3), Eq(0));
  edit.clear();
  EXPECT_THAT(edit.empty(), Eq(true));
}

TEST_F(HamtFlatSetTest, PersistentInsertionPreservesTheOriginalAndDuplicates) {
  auto empty = Set::TryCreate();
  if (!empty) {
    FAIL() << "set creation failed";
    return;
  }
  auto inserted = empty->try_insert(1);
  auto* const first = std::get_if<std::pair<Set, bool>>(&inserted);
  ASSERT_THAT(first, NotNull());
  EXPECT_THAT(first->second, Eq(true));
  EXPECT_THAT(*empty, IsEmpty());
  EXPECT_THAT(first->first, UnorderedElementsAre(1));
  auto duplicate = first->first.try_insert(1);
  auto* const repeated = std::get_if<std::pair<Set, bool>>(&duplicate);
  ASSERT_THAT(repeated, NotNull());
  EXPECT_THAT(repeated->second, Eq(false));
  EXPECT_THAT(repeated->first, UnorderedElementsAre(1));
  EXPECT_THAT(first->first.begin() == repeated->first.begin(), Eq(false));
}

TEST_F(HamtFlatSetTest, TransientConversionIsConsumingAndLeavesReusableEmptyValues) {
  auto original = Set::TryCreate();
  if (!original) {
    FAIL() << "set creation failed";
    return;
  }
  auto transient = original->transient();
  auto insertion = transient.try_insert(1);
  const auto* const result = std::get_if<std::pair<Set::iterator, bool>>(&insertion);
  ASSERT_THAT(result, NotNull());
  EXPECT_THAT(result->second, Eq(true));
  EXPECT_THAT(*result->first, Eq(1));
  EXPECT_THAT(*original, IsEmpty());
  auto persistent = std::move(transient).persistent();
  EXPECT_THAT(persistent, UnorderedElementsAre(1));
  EXPECT_THAT(transient.empty(), Eq(true));
  EXPECT_THAT(std::holds_alternative<HamtError>(transient.try_insert(2)), Eq(false));
  EXPECT_THAT(persistent.contains(2), Eq(false));
  EXPECT_THAT(transient.contains(2), Eq(true));
  EXPECT_THAT(transient.try_erase(2), Eq(Set::transient_type::erasure_result(std::size_t{1})));
  EXPECT_THAT(transient.empty(), Eq(true));
}

TEST_F(HamtFlatSetTest, MaximumSizeAndAllocationExhaustionAreDistinct) {
  constexpr HamtOptions kOptions{.maximum_size = 1};
  using SmallSet = HamtFlatSet<int, std::hash<int>, std::equal_to<>, kOptions>;
  auto small = SmallSet::TryCreate();
  if (!small) {
    FAIL() << "set creation failed";
    return;
  }
  auto transient = small->transient();
  EXPECT_THAT(std::holds_alternative<HamtError>(transient.try_insert(1)), Eq(false));
  EXPECT_THAT(transient.try_insert(2), Eq(SmallSet::transient_type::insertion_result(HamtError::kMaxSizeExceeded)));
  EXPECT_THAT(std::holds_alternative<HamtError>(transient.try_insert(1)), Eq(false));

  using BoundedSet =
      HamtFlatSet<int, std::hash<int>, std::equal_to<>, HamtOptions{}, mbo::memory::InlineBlockSource<1>>;
  auto bounded = BoundedSet::TryCreate();
  if (!bounded) {
    FAIL() << "set creation failed";
    return;
  }
  EXPECT_THAT(bounded->try_insert(1), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(*bounded, IsEmpty());
}
}  // namespace
}  // namespace mbo::container
