// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/hamt_node_set.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/hamt_options.h"
#include "mbo/memory/block_source.h"

namespace mbo::container {
namespace {
using ::testing::Eq;
using ::testing::NotNull;
using ::testing::UnorderedElementsAre;
using ::testing::VariantWith;
using Set = HamtNodeSet<int>;

struct HamtNodeSetTest : ::testing::Test {};

struct CollisionHash final {
  constexpr std::uint64_t operator()(int /*key*/) const noexcept { return 7; }
};

struct MoveOnlyKey final {
  explicit MoveOnlyKey(int value) noexcept : value(value) {}

  MoveOnlyKey(const MoveOnlyKey&) = delete;
  MoveOnlyKey& operator=(const MoveOnlyKey&) = delete;

  MoveOnlyKey(MoveOnlyKey&& other) noexcept : value(std::exchange(other.value, -1)) {}

  MoveOnlyKey& operator=(MoveOnlyKey&&) noexcept = default;
  ~MoveOnlyKey() = default;
  friend bool operator==(const MoveOnlyKey&, const MoveOnlyKey&) noexcept = default;
  int value;
};

struct MoveOnlyHash final {
  std::uint64_t operator()(const MoveOnlyKey& key) const noexcept { return static_cast<std::uint64_t>(key.value); }
};

struct AllocationBudget final {
  std::size_t remaining = 0;
  std::size_t acquired = 0;
  std::size_t released = 0;
};

struct BudgetSource final {
  explicit BudgetSource(AllocationBudget& budget) noexcept : budget(&budget) {}

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) const noexcept {
    if (budget->remaining == 0) {
      return std::nullopt;
    }
    const auto block = mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
    if (block) {
      --budget->remaining;
      ++budget->acquired;
    }
    return block;
  }

  void Release(mbo::memory::MemoryBlock block) const noexcept {
    ++budget->released;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }

  AllocationBudget* budget;
};

TEST_F(HamtNodeSetTest, TopologyExhaustionReclaimsTheNewPayloadAndPreservesSnapshots) {
  using BudgetSet = HamtNodeSet<int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget{.remaining = 2};
  {
    auto created = BudgetSet::TryCreate(std::hash<int>{}, std::equal_to<>{}, budget);
    if (!created) {
      FAIL() << "allocation-domain creation failed";
      return;
    }
    auto edit = std::move(*created).transient();
    EXPECT_THAT(edit.insert(42).second, Eq(true));
    const auto snapshot = std::move(edit).persistent();
    auto child = snapshot.transient();
    budget.remaining = 1;
    const auto releases = budget.released;
    EXPECT_THAT(child.try_insert(99), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
    EXPECT_THAT(budget.released, Eq(releases + 1));
    EXPECT_THAT(child, UnorderedElementsAre(42));
    EXPECT_THAT(snapshot, UnorderedElementsAre(42));
    auto duplicate = child.try_insert(42);
    const auto* const result = std::get_if<std::pair<BudgetSet::iterator, bool>>(&duplicate);
    ASSERT_THAT(result, NotNull());
    EXPECT_THAT(result->second, Eq(false));
  }
  EXPECT_THAT(budget.acquired, Eq(budget.released));
}

TEST_F(HamtNodeSetTest, ErasureAllocationFailurePreservesPersistentAndTransientValues) {
  using BudgetSet = HamtNodeSet<int, CollisionHash, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget{.remaining = 8};
  auto created = BudgetSet::TryCreate(CollisionHash{}, std::equal_to<>{}, budget);
  if (!created) {
    FAIL() << "allocation-domain creation failed";
    return;
  }
  auto edit = std::move(*created).transient();
  EXPECT_THAT(edit.insert(42).second, Eq(true));
  EXPECT_THAT(edit.insert(99).second, Eq(true));
  const auto snapshot = std::move(edit).persistent();

  budget.remaining = 0;
  EXPECT_THAT(snapshot.try_erase(42), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(snapshot, UnorderedElementsAre(42, 99));

  auto child = snapshot.transient();
  EXPECT_THAT(child.try_erase(42), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(child, UnorderedElementsAre(42, 99));
  EXPECT_THAT(snapshot, UnorderedElementsAre(42, 99));
}

TEST_F(HamtNodeSetTest, RvalueInsertionSupportsMoveOnlyKeysWithoutConsumingDuplicates) {
  using MoveSet = HamtNodeSet<MoveOnlyKey, MoveOnlyHash>;
  const MoveSet empty;
  MoveOnlyKey key(42);
  auto [one, inserted] = empty.insert(std::move(key));
  EXPECT_THAT(inserted, Eq(true));
  // NOLINTNEXTLINE(bugprone-use-after-move): verifies the key's specified moved-from state.
  EXPECT_THAT(key.value, Eq(-1));
  EXPECT_THAT(one.begin()->value, Eq(42));
  MoveOnlyKey duplicate(42);
  auto [same, changed] = one.insert(std::move(duplicate));
  EXPECT_THAT(changed, Eq(false));
  // NOLINTNEXTLINE(bugprone-use-after-move): duplicate insertion must not consume the key.
  EXPECT_THAT(duplicate.value, Eq(42));
  EXPECT_THAT(same.size(), Eq(1));
  auto edit = same.transient();
  MoveOnlyKey next(99);
  auto [position, added] = edit.insert(std::move(next));
  EXPECT_THAT(added, Eq(true));
  // NOLINTNEXTLINE(bugprone-use-after-move): verifies the key's specified moved-from state.
  EXPECT_THAT(next.value, Eq(-1));
  ASSERT_THAT(position == edit.end(), Eq(false));
  EXPECT_THAT(position->value, Eq(99));
}

TEST_F(HamtNodeSetTest, CloningRebuildsPayloadsAndConsumesOnlyOnCompleteSuccess) {
  const Set empty;
  auto [one, inserted] = empty.insert(42);
  EXPECT_THAT(inserted, Eq(true));
  auto cloned = one.try_clone_to<mbo::memory::NewDeleteBlockSource>();
  ASSERT_THAT(cloned.has_value(), Eq(true));
  const auto independent = std::move(cloned).value_or(Set{});
  EXPECT_THAT(independent, UnorderedElementsAre(42));
  EXPECT_THAT(std::addressof(*independent.find(42)) == std::addressof(*one.find(42)), Eq(false));
  auto failed = std::move(one).try_clone_to<mbo::memory::InlineBlockSource<1>>();
  EXPECT_THAT(failed.has_value(), Eq(false));
  // NOLINTNEXTLINE(bugprone-use-after-move): failed rvalue cloning preserves the source by contract.
  EXPECT_THAT(one, UnorderedElementsAre(42));
  auto consumed = std::move(one).try_clone_to<mbo::memory::NewDeleteBlockSource>();
  EXPECT_THAT(consumed.has_value(), Eq(true));
  // NOLINTNEXTLINE(bugprone-use-after-move): successful rvalue cloning empties the source by contract.
  EXPECT_THAT(one.empty(), Eq(true));
}

TEST_F(HamtNodeSetTest, MaximumSizeRejectsNewKeysButAllowsDuplicates) {
  constexpr HamtOptions kOneEntry{.fragment_bits = 5, .maximum_size = 1};
  using LimitedSet = HamtNodeSet<int, std::hash<int>, std::equal_to<>, kOneEntry>;
  LimitedSet empty;
  auto edit = std::move(empty).transient();
  EXPECT_THAT(edit.insert(42).second, Eq(true));
  EXPECT_THAT(edit.insert(42).second, Eq(false));
  EXPECT_THAT(edit.try_insert(99), VariantWith<HamtError>(Eq(HamtError::kMaxSizeExceeded)));
  EXPECT_THAT(edit, UnorderedElementsAre(42));
  using ExhaustedSet =
      HamtNodeSet<int, std::hash<int>, std::equal_to<>, HamtOptions{}, mbo::memory::InlineBlockSource<1>>;
  const ExhaustedSet exhausted;
  EXPECT_THAT(exhausted.try_insert(42), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(exhausted.empty(), Eq(true));
}

TEST_F(HamtNodeSetTest, CollisionInsertionDuplicatesAndErasurePreserveSnapshots) {
  using CollisionSet = HamtNodeSet<int, CollisionHash>;
  CollisionSet empty;
  auto edit = std::move(empty).transient();
  EXPECT_THAT(edit.insert(42).second, Eq(true));
  EXPECT_THAT(edit.insert(99).second, Eq(true));
  const auto* const address = std::addressof(*edit.find(42));
  EXPECT_THAT(edit.insert(42).second, Eq(false));
  EXPECT_THAT(std::addressof(*edit.find(42)), Eq(address));
  const auto snapshot = std::move(edit).persistent();
  EXPECT_THAT(snapshot, UnorderedElementsAre(42, 99));
  auto [erased, changed] = snapshot.erase(42);
  EXPECT_THAT(changed, Eq(true));
  EXPECT_THAT(erased, UnorderedElementsAre(99));
  EXPECT_THAT(snapshot, UnorderedElementsAre(42, 99));
}

TEST_F(HamtNodeSetTest, PersistentAndTransientMutationKeepUnchangedKeyAddresses) {
  const Set empty;
  auto [one, inserted] = empty.insert(42);
  EXPECT_THAT(inserted, Eq(true));
  const auto* const address = std::addressof(*one.find(42));
  auto [two, changed] = one.insert(99);
  EXPECT_THAT(changed, Eq(true));
  EXPECT_THAT(std::addressof(*two.find(42)), Eq(address));
  auto edit = two.transient();
  EXPECT_THAT(edit.insert(123).second, Eq(true));
  EXPECT_THAT(std::addressof(*edit.find(42)), Eq(address));
  EXPECT_THAT(edit.erase(99), Eq(1));
  EXPECT_THAT(two.contains(99), Eq(true));
  EXPECT_THAT(edit.contains(99), Eq(false));
}

}  // namespace
}  // namespace mbo::container
