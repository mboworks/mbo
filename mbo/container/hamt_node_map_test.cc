// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/hamt_node_map.h"

#include <memory>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container {
namespace {
using ::testing::Eq;
using ::testing::NotNull;
using ::testing::VariantWith;

struct HamtNodeMapTest : ::testing::Test {
  template<std::size_t Bits>
  void CheckFragmentWidth() {
    using Map = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{.fragment_bits = Bits}>;
    auto edit = Map{}.transient();
    for (int key = 0; key < 64; ++key) {
      EXPECT_THAT(edit.insert({key * 33, key}).second, Eq(true));
    }
    const auto snapshot = std::move(edit).persistent();
    EXPECT_THAT(snapshot.size(), Eq(64));
    auto child = snapshot.transient();
    for (int key = 0; key < 64; ++key) {
      EXPECT_THAT(snapshot.at(key * 33), Eq(key));
      child.at(key * 33) += 100;
    }
    for (int key = 0; key < 64; ++key) {
      EXPECT_THAT(snapshot.at(key * 33), Eq(key));
      EXPECT_THAT(std::as_const(child).at(key * 33), Eq(key + 100));
      EXPECT_THAT(child.erase(key * 33), Eq(1));
    }
    EXPECT_THAT(child.empty(), Eq(true));
    EXPECT_THAT(snapshot.size(), Eq(64));
  }
};

TEST_F(HamtNodeMapTest, FourBitFragmentsPreserveSnapshotsAcrossAllEdits) {
  CheckFragmentWidth<4>();
}

TEST_F(HamtNodeMapTest, FiveBitFragmentsPreserveSnapshotsAcrossAllEdits) {
  CheckFragmentWidth<5>();
}

TEST_F(HamtNodeMapTest, SixBitFragmentsPreserveSnapshotsAcrossAllEdits) {
  CheckFragmentWidth<6>();
}

TEST_F(HamtNodeMapTest, SevenBitFragmentsPreserveSnapshotsAcrossAllEdits) {
  CheckFragmentWidth<7>();
}

struct AllocationBudget final {
  std::size_t remaining = 0;
  std::size_t acquired = 0;
  std::size_t released = 0;
};

TEST_F(HamtNodeMapTest, MoveOnlyMappedValuesShareOwnershipWithoutConsumingDuplicates) {
  using Map = HamtNodeMap<int, std::unique_ptr<int>>;
  Map::value_type entry(42, std::make_unique<int>(99));
  auto inserted = Map{}.insert(std::move(entry));
  EXPECT_THAT(entry.second.get(), Eq(nullptr));
  EXPECT_THAT(*inserted.first.at(42), Eq(99));
  const Map snapshot = inserted.first;
  EXPECT_THAT(snapshot.at(42).get(), Eq(inserted.first.at(42).get()));
  Map::value_type duplicate(42, std::make_unique<int>(100));
  auto unchanged = snapshot.insert(std::move(duplicate));
  EXPECT_THAT(unchanged.second, Eq(false));
  ASSERT_THAT(duplicate.second.get(), NotNull());
  EXPECT_THAT(*duplicate.second, Eq(100));
  EXPECT_THAT(unchanged.first.erase(42).first.empty(), Eq(true));
  EXPECT_THAT(*snapshot.at(42), Eq(99));
}

struct BudgetSource final {
  explicit BudgetSource(AllocationBudget& budget) noexcept : budget(&budget) {}

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
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

  void Release(mbo::memory::MemoryBlock block) noexcept {
    ++budget->released;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }

  AllocationBudget* budget;
};

TEST_F(HamtNodeMapTest, SnapshotsKeepTheirSourceAliveAfterOriginalContainersDisappear) {
  using Map = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget{.remaining = 2};
  std::optional<Map> retained;
  {
    auto created = Map::TryCreate(std::hash<int>{}, std::equal_to<>{}, budget);
    if (!created) {
      FAIL() << "source domain creation failed";
      return;
    }
    auto inserted = created->insert({42, 99});
    retained.emplace(inserted.first);
  }
  if (!retained) {
    FAIL() << "snapshot was not retained";
    return;
  }
  EXPECT_THAT(retained->at(42), Eq(99));
  EXPECT_THAT(budget.released, Eq(0));
  retained.reset();
  EXPECT_THAT(budget.acquired, Eq(budget.released));
}

TEST_F(HamtNodeMapTest, IntermediateAllocationFailuresReleaseTemporaryOwnership) {
  using Map = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget{.remaining = 2};
  {
    auto created = Map::TryCreate(std::hash<int>{}, std::equal_to<>{}, budget);
    if (!created) {
      FAIL() << "source domain creation failed";
      return;
    }
    auto inserted = created->insert({42, 99});
    const auto& snapshot = inserted.first;
    const auto* const address = &snapshot.at(42);
    budget.remaining = 1;
    const auto released = budget.released;
    EXPECT_THAT(snapshot.try_insert({7, 8}), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    EXPECT_THAT(budget.released, Eq(released + 1));
    EXPECT_THAT(&snapshot.at(42), Eq(address));
    budget.remaining = 1;
    EXPECT_THAT(
        snapshot.try_update(42, [](int& value) noexcept { value = 100; }),
        VariantWith<HamtError>(HamtError::kAllocationExhausted));
    EXPECT_THAT(snapshot.at(42), Eq(99));
    EXPECT_THAT(&snapshot.at(42), Eq(address));
    budget.remaining = 0;
    auto missing = snapshot.try_update(7, [](int& value) noexcept { value = 100; });
    const auto* const unchanged = std::get_if<std::pair<Map, bool>>(&missing);
    ASSERT_THAT(unchanged, NotNull());
    EXPECT_THAT(unchanged->second, Eq(false));
  }
  EXPECT_THAT(budget.acquired, Eq(budget.released));
}

TEST_F(HamtNodeMapTest, MutablePreparationFailurePreservesValuesAndMissingLookupDoesNotAllocate) {
  using Map = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget{.remaining = 2};
  {
    auto created = Map::TryCreate(std::hash<int>{}, std::equal_to<>{}, budget);
    if (!created) {
      FAIL() << "source domain creation failed";
      return;
    }
    auto inserted = created->insert({42, 99});
    auto edit = inserted.first.transient();
    budget.remaining = 0;
    const auto acquired = budget.acquired;
    auto missing = edit.try_find(7);
    const auto* const end = std::get_if<Map::transient_type::iterator>(&missing);
    ASSERT_THAT(end, NotNull());
    EXPECT_THAT(*end == edit.end(), Eq(true));
    EXPECT_THAT(budget.acquired, Eq(acquired));
    EXPECT_THAT(edit.try_begin(), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    budget.remaining = 1;
    EXPECT_THAT(edit.try_begin(), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    EXPECT_THAT(std::as_const(edit).at(42), Eq(99));
    EXPECT_THAT(inserted.first.at(42), Eq(99));
    budget.remaining = 1;
    auto prepared = edit.try_begin();
    auto* const position = std::get_if<Map::transient_type::iterator>(&prepared);
    ASSERT_THAT(position, NotNull());
    (*position)->second = 100;
    EXPECT_THAT(std::as_const(edit).at(42), Eq(100));
    EXPECT_THAT(inserted.first.at(42), Eq(99));
  }
  EXPECT_THAT(budget.acquired, Eq(budget.released));
}

TEST_F(HamtNodeMapTest, HeterogeneousLookupAndEditUseTheStoredKeyWithoutChangingItsType) {
  using Map = HamtNodeMap<int, int>;
  auto inserted = Map{}.insert({42, 99});
  EXPECT_THAT(inserted.first.contains(short{42}), Eq(true));
  EXPECT_THAT(inserted.first.count(short{7}), Eq(0));
  EXPECT_THAT(inserted.first.find(short{42})->second, Eq(99));
  auto edit = inserted.first.transient();
  auto found = edit.find(short{42});
  found->second = 100;
  EXPECT_THAT(std::as_const(edit).at(short{42}), Eq(100));
  EXPECT_THAT(inserted.first.at(short{42}), Eq(99));
}

TEST_F(HamtNodeMapTest, PersistentMappedEditDetachesPayloadAndPreservesSnapshot) {
  using Map = HamtNodeMap<int, int>;
  Map empty;
  auto inserted = empty.insert({42, 99});
  Map original = std::move(inserted.first);
  auto result = original.try_update(42, [](int& value) noexcept { value = 100; });
  auto* const updated = std::get_if<std::pair<Map, bool>>(&result);
  ASSERT_THAT(updated, NotNull());
  EXPECT_THAT(updated->second, Eq(true));
  EXPECT_THAT(updated->first.at(42), Eq(100));
  EXPECT_THAT(original.at(42), Eq(99));
  EXPECT_THAT(empty.empty(), Eq(true));
}

struct CollisionHash final {
  std::size_t operator()(int) const noexcept { return 7; }
};

TEST_F(HamtNodeMapTest, CollisionEditsAndErasurePreserveSnapshotsAndSurvivingAddresses) {
  using Map = HamtNodeMap<int, int, CollisionHash>;
  auto first = Map{}.insert({1, 10});
  const auto* const address = &first.first.at(1);
  auto second = first.first.insert({2, 20});
  EXPECT_THAT(&second.first.at(1), Eq(address));
  auto duplicate = second.first.insert({1, 100});
  EXPECT_THAT(duplicate.second, Eq(false));
  EXPECT_THAT(duplicate.first.at(1), Eq(10));
  auto edit = second.first.transient();
  EXPECT_THAT(edit.erase(2), Eq(1));
  EXPECT_THAT(edit.erase(2), Eq(0));
  EXPECT_THAT(&std::as_const(edit).at(1), Eq(address));
  edit.at(1) = 11;
  EXPECT_THAT(first.first.at(1), Eq(10));
  EXPECT_THAT(second.first.at(2), Eq(20));
  EXPECT_THAT(edit.contains(2), Eq(false));
}

TEST_F(HamtNodeMapTest, SizeLimitAllowsDuplicatesAndEmptyStorageReportsExhaustion) {
  using Limited = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{.maximum_size = 1}>;
  auto inserted = Limited{}.insert({1, 10});
  EXPECT_THAT(inserted.first.insert({1, 20}).second, Eq(false));
  EXPECT_THAT(inserted.first.try_insert({2, 20}), VariantWith<HamtError>(HamtError::kMaxSizeExceeded));
  EXPECT_THAT(inserted.first.at(1), Eq(10));
  using Bounded =
      HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, mbo::memory::InlineBlockSource<1>>;
  Bounded empty;
  EXPECT_THAT(empty.try_insert({1, 10}), VariantWith<HamtError>(HamtError::kAllocationExhausted));
  EXPECT_THAT(empty.empty(), Eq(true));
  auto clone = std::move(inserted.first).try_clone_to<mbo::memory::InlineBlockSource<1>>();
  EXPECT_THAT(clone.has_value(), Eq(false));
  EXPECT_THAT(inserted.first.at(1), Eq(10));
}

TEST_F(HamtNodeMapTest, TransientAccessAndIterationPreservePublishedSnapshots) {
  using Map = HamtNodeMap<int, int>;
  Map empty;
  auto inserted = empty.insert({42, 99});
  auto edit = inserted.first.transient();
  edit.at(42) = 100;
  EXPECT_THAT(inserted.first.at(42), Eq(99));
  EXPECT_THAT(edit.at(42), Eq(100));
  auto added = edit.insert({7, 8});
  EXPECT_THAT(added.second, Eq(true));
  added.first->second = 9;
  EXPECT_THAT(edit.at(7), Eq(9));
  for (auto& entry : edit) {
    entry.second += 1;
  }
  EXPECT_THAT(edit.at(42), Eq(101));
  EXPECT_THAT(edit.at(7), Eq(10));
  auto snapshot = std::move(edit).persistent();
  EXPECT_THAT(snapshot.at(42), Eq(101));
  EXPECT_THAT(edit.empty(), Eq(true));
}

TEST_F(HamtNodeMapTest, IndependentCloneAllocatesIndependentPayloads) {
  using Map = HamtNodeMap<int, int>;
  auto inserted = Map{}.insert({42, 99});
  auto cloned = inserted.first.try_clone_to<mbo::memory::NewDeleteBlockSource>();
  ASSERT_THAT(cloned.has_value(), Eq(true));
  const auto copy = std::move(cloned).value_or(Map{});
  EXPECT_THAT(copy.at(42), Eq(99));
  EXPECT_THAT(&copy.at(42) == &inserted.first.at(42), Eq(false));
}

TEST_F(HamtNodeMapTest, DefaultInsertionClearSwapAndConsumingCloneLeaveReusableContainers) {
  using Map = HamtNodeMap<int, int>;
  auto edit = Map{}.transient();
  edit[42] = 99;
  EXPECT_THAT(edit.at(42), Eq(99));
  EXPECT_THAT(edit[7], Eq(0));
  auto other = Map{}.transient();
  other[1] = 10;
  edit.swap(other);
  EXPECT_THAT(edit.at(1), Eq(10));
  EXPECT_THAT(other.at(42), Eq(99));
  auto cloned = std::move(other).try_clone_to<mbo::memory::NewDeleteBlockSource>();
  ASSERT_THAT(cloned.has_value(), Eq(true));
  const auto copy = std::move(cloned).value_or(Map{});
  EXPECT_THAT(copy.at(42), Eq(99));
  EXPECT_THAT(copy.at(7), Eq(0));
  EXPECT_THAT(other.empty(), Eq(true));
  other[2] = 20;
  EXPECT_THAT(other.at(2), Eq(20));
  other.clear();
  EXPECT_THAT(other.empty(), Eq(true));
}
}  // namespace
}  // namespace mbo::container
