// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/hamt_flat_set.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

namespace mbo::container {
namespace {
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::NotNull;
using ::testing::UnorderedElementsAre;
using ::testing::VariantWith;
using Set = HamtFlatSet<int>;

struct HamtFlatSetTest : ::testing::Test {};

struct BudgetSource final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  explicit BudgetSource(std::size_t& remaining) noexcept : remaining(remaining) {}

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    if (remaining == 0) {
      return std::nullopt;
    }
    --remaining;
    return mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
  }

  static void Release(mbo::memory::MemoryBlock block) noexcept { mbo::memory::NewDeleteBlockSource::Release(block); }

  std::size_t& remaining;
};

TEST_F(HamtFlatSetTest, FailedErasurePreservesPersistentAndTransientContents) {
  using BudgetSet = HamtFlatSet<int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  std::size_t remaining = 8;
  auto created = BudgetSet::try_create(std::hash<int>{}, std::equal_to<>{}, remaining);
  if (!created) {
    FAIL() << "set creation failed";
    return;
  }
  auto edit = created->transient();
  EXPECT_THAT(edit.insert(1).second, Eq(true));
  EXPECT_THAT(edit.insert(2).second, Eq(true));
  const auto snapshot = std::move(edit).persistent();
  // BudgetSource observes this reference through type-erased tree storage.
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  remaining = 0;
  EXPECT_THAT(snapshot.try_erase(1), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  auto shared_edit = snapshot.transient();
  EXPECT_THAT(shared_edit.try_erase(1), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(snapshot, UnorderedElementsAre(1, 2));
  EXPECT_THAT(shared_edit, UnorderedElementsAre(1, 2));
  EXPECT_THAT(snapshot.size(), Eq(2));
}

TEST_F(HamtFlatSetTest, EmptyPersistentAndTransientDiagnosticsHaveNoReachableNodes) {
  Set container;
  EXPECT_THAT(container.structural_diagnostics().nodes, Eq(0));
  auto transient = std::move(container).transient();
  EXPECT_THAT(transient.structural_diagnostics().entries, Eq(0));
}

TEST_F(HamtFlatSetTest, CallerOwnedControlStorageSupportsInsertionAndReclamation) {
  mbo::memory::InlineBlockSource<4'096> storage;
  {
    auto created = Set::try_create_in(storage, std::hash<int>{}, std::equal_to<>{});
    if (!created) {
      FAIL() << "control-storage domain creation failed";
      return;
    }
    auto set = std::move(*created).insert(1).first;
    created.reset();
    EXPECT_THAT(set.contains(1), Eq(true));
    EXPECT_THAT(Set::try_create_in(storage, std::hash<int>{}, std::equal_to<>{}).has_value(), Eq(false));
  }
  EXPECT_THAT(Set::try_create_in(storage, std::hash<int>{}, std::equal_to<>{}).has_value(), Eq(true));
  mbo::memory::InlineBlockSource<1> exhausted;
  EXPECT_THAT(Set::try_create_in(exhausted, std::hash<int>{}, std::equal_to<>{}).has_value(), Eq(false));
}

TEST_F(HamtFlatSetTest, PublicCloneReturnsTheDestinationSourceSpecialization) {
  using BoundedSet =
      HamtFlatSet<int, std::hash<int>, std::equal_to<>, HamtOptions{}, mbo::memory::InlineBlockSource<1'024>>;
  BoundedSet empty;
  auto edit = std::move(empty).transient();
  EXPECT_THAT(edit.insert(1).second, Eq(true));
  auto original = std::move(edit).persistent();
  auto cloned = original.try_clone_to<mbo::memory::NewDeleteBlockSource>();
  static_assert(std::same_as<decltype(cloned)::value_type, Set>);
  if (!cloned) {
    FAIL() << "cross-source clone failed";
    return;
  }
  EXPECT_THAT(*cloned, UnorderedElementsAre(1));
  EXPECT_THAT(original, UnorderedElementsAre(1));
  auto consumed = std::move(original).try_clone_to<mbo::memory::NewDeleteBlockSource>();
  EXPECT_THAT(consumed.has_value(), Eq(true));
  // The consuming overload deliberately leaves a successfully cloned source empty.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_THAT(original, IsEmpty());
  // Reuse of that specified valid empty state is part of the public contract.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  auto reuse = std::move(original).transient();
  EXPECT_THAT(reuse.insert(2).second, Eq(true));
  auto failed = std::move(reuse).try_clone_to<mbo::memory::InlineBlockSource<1>>();
  EXPECT_THAT(failed.has_value(), Eq(false));
  // A failed consuming clone must preserve the source transient.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_THAT(reuse.contains(2), Eq(true));
}

struct CollisionHash final {
  std::uint64_t operator()([[maybe_unused]] std::int64_t key) const noexcept { return seed; }

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

TEST_F(HamtFlatSetTest, CollisionLimitRejectsOnlyNewEqualHashKeys) {
  constexpr HamtOptions kOptions{.maximum_collision_size = 2};
  using CollisionSet = HamtFlatSet<int, CollisionHash, std::equal_to<>, kOptions>;
  CollisionSet empty;
  auto edit = std::move(empty).transient();
  EXPECT_THAT(edit.insert(1).second, Eq(true));
  EXPECT_THAT(edit.insert(2).second, Eq(true));
  const auto duplicate = edit.try_insert(2);
  const auto* const duplicate_result = std::get_if<std::pair<CollisionSet::iterator, bool>>(&duplicate);
  ASSERT_THAT(duplicate_result, NotNull());
  EXPECT_THAT(duplicate_result->second, Eq(false));
  EXPECT_THAT(
      edit.try_insert(3), Eq(CollisionSet::transient_type::insertion_result(HamtError::kCollisionLimitExceeded)));
  EXPECT_THAT(edit, UnorderedElementsAre(1, 2));
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
  // persistent() specifies that the consumed transient becomes reusable and empty.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_THAT(edit.insert(3).second, Eq(true));
  EXPECT_THAT(edit.erase(3), Eq(1));
  EXPECT_THAT(edit.erase(3), Eq(0));
  edit.clear();
  EXPECT_THAT(edit.empty(), Eq(true));
}

TEST_F(HamtFlatSetTest, PersistentInsertionPreservesTheOriginalAndDuplicates) {
  auto empty = Set::try_create();
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
  auto original = Set::try_create();
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
  // persistent() specifies that the consumed transient becomes reusable and empty.
  // NOLINTNEXTLINE(bugprone-use-after-move)
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
  auto small = SmallSet::try_create();
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
  auto bounded = BoundedSet::try_create();
  if (!bounded) {
    FAIL() << "set creation failed";
    return;
  }
  EXPECT_THAT(bounded->try_insert(1), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(*bounded, IsEmpty());
}
}  // namespace
}  // namespace mbo::container
