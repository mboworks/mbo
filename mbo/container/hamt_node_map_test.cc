// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/hamt_node_map.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container {
namespace {
using ::testing::Eq;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::Ne;
using ::testing::NotNull;
using ::testing::Pair;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;
using ::testing::VariantWith;

template<typename Map>
concept SupportsPayloadUpdate = requires(Map& map) { map.try_update(0, [](auto&) noexcept {}); };

template<typename Map>
concept SupportsIndependentClone =
    requires(const Map& map) { map.template try_clone_to<mbo::memory::NewDeleteBlockSource>(); };

using CopyableMap = HamtNodeMap<int, int>;
using MoveOnlyMap = HamtNodeMap<int, std::unique_ptr<int>>;
static_assert(SupportsPayloadUpdate<CopyableMap>);
static_assert(SupportsPayloadUpdate<CopyableMap::transient_type>);
static_assert(!SupportsPayloadUpdate<MoveOnlyMap>);
static_assert(!SupportsPayloadUpdate<MoveOnlyMap::transient_type>);
static_assert(SupportsIndependentClone<CopyableMap>);
static_assert(!SupportsIndependentClone<MoveOnlyMap>);

template<typename Map>
concept SupportsMutablePayloadAccess = requires(Map& map) {
  map.try_begin();
  map.try_find(0);
  map.try_at(0);
  map.try_get_or_insert(0);
};
static_assert(SupportsMutablePayloadAccess<CopyableMap::transient_type>);
static_assert(!SupportsMutablePayloadAccess<MoveOnlyMap::transient_type>);

enum class DeepMutation { kUpdate, kInsert, kErase };

struct HamtNodeMapTest : ::testing::Test {
  template<std::size_t Bits, DeepMutation Mutation = DeepMutation::kUpdate>
  void CheckDeepMutationFailure();

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

TEST_F(HamtNodeMapTest, EmptyPersistentAndTransientDiagnosticsHaveNoReachableNodes) {
  CopyableMap container;
  EXPECT_THAT(container.structural_diagnostics().nodes, Eq(0));
  auto transient = std::move(container).transient();
  EXPECT_THAT(transient.structural_diagnostics().entries, Eq(0));
  EXPECT_THAT(transient.structural_diagnostics().entry_allocation_bytes, Eq(0));
  EXPECT_THAT(transient.insert({42, 10}).second, Eq(true));
  const auto measured = transient.structural_diagnostics();
  EXPECT_THAT(measured.entries, Eq(1));
  EXPECT_THAT(measured.entry_allocation_bytes >= sizeof(CopyableMap::value_type), Eq(true));
  const auto snapshot = std::move(transient).persistent();
  EXPECT_THAT(snapshot.structural_diagnostics().entry_allocation_bytes, Eq(measured.entry_allocation_bytes));
}

TEST_F(HamtNodeMapTest, FourBitFragmentsPreserveSnapshotsAcrossAllEdits) {
  CheckFragmentWidth<4>();
}

TEST_F(HamtNodeMapTest, CallerOwnedControlStorageIsRetainedByPublishedMapSnapshots) {
  mbo::memory::InlineBlockSource<4'096> storage;
  {
    auto created = CopyableMap::try_create_in(storage, std::hash<int>{}, std::equal_to<>{});
    if (!created) {
      FAIL() << "control-storage domain creation failed";
      return;
    }
    auto inserted = std::move(*created).insert({1, 99}).first;
    created.reset();
    EXPECT_THAT(inserted.at(1), Eq(99));
    EXPECT_THAT(CopyableMap::try_create_in(storage, std::hash<int>{}, std::equal_to<>{}).has_value(), Eq(false));
  }
  EXPECT_THAT(CopyableMap::try_create_in(storage, std::hash<int>{}, std::equal_to<>{}).has_value(), Eq(true));
  mbo::memory::InlineBlockSource<1> exhausted;
  EXPECT_THAT(CopyableMap::try_create_in(exhausted, std::hash<int>{}, std::equal_to<>{}).has_value(), Eq(false));
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

struct CollisionHash final {
  std::size_t operator()(int /*unused*/) const noexcept { return 7; }
};

TEST_F(HamtNodeMapTest, MoveOnlyMappedValuesShareOwnershipWithoutConsumingDuplicates) {
  using Map = HamtNodeMap<int, std::unique_ptr<int>>;
  Map::value_type entry(42, std::make_unique<int>(99));
  auto inserted = Map{}.insert(std::move(entry));
  // NOLINTNEXTLINE(bugprone-use-after-move): verifies that successful insertion consumes the mapped value.
  EXPECT_THAT(entry.second.get(), Eq(nullptr));
  EXPECT_THAT(*inserted.first.at(42), Eq(99));
  const Map snapshot = inserted.first;
  EXPECT_THAT(snapshot.at(42).get(), Eq(inserted.first.at(42).get()));
  Map::value_type duplicate(42, std::make_unique<int>(100));
  auto unchanged = snapshot.insert(std::move(duplicate));
  EXPECT_THAT(unchanged.second, Eq(false));
  // NOLINTNEXTLINE(bugprone-use-after-move): duplicate insertion must not consume the mapped value.
  ASSERT_THAT(duplicate.second.get(), NotNull());
  // NOLINTNEXTLINE(bugprone-use-after-move): duplicate insertion must not consume the mapped value.
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

struct FullWidthIdentityHash final {
  std::uint64_t operator()(std::uint64_t key) const noexcept { return key; }
};

template<std::size_t Bits, DeepMutation Mutation>
void HamtNodeMapTest::CheckDeepMutationFailure() {
  using Map = HamtNodeMap<
      std::uint64_t, int, FullWidthIdentityHash, std::equal_to<>, HamtOptions{.fragment_bits = Bits}, BudgetSource>;
  constexpr auto kHighBit = std::uint64_t{1} << 63;
  constexpr auto kNewKey = std::uint64_t{1} << 62;
  constexpr auto kMaxAllocations = (2 * ((std::numeric_limits<std::uint64_t>::digits + Bits - 1) / Bits)) + 4;
  AllocationBudget budget{.remaining = 4 * kMaxAllocations};
  {
    auto created = Map::try_create(FullWidthIdentityHash{}, std::equal_to<>{}, budget);
    if (!created) {
      FAIL() << "source domain creation failed";
      return;
    }
    auto first = created->insert({0, 10});
    auto second = first.first.insert({kHighBit, 20});
    const auto& snapshot = second.first;
    const auto* const original = &snapshot.at(0);
    const auto* const sibling = &snapshot.at(kHighBit);
    bool succeeded = false;
    std::size_t failures = 0;
    for (std::size_t allowance = 0; allowance <= kMaxAllocations; ++allowance) {
      SCOPED_TRACE(allowance);
      budget.remaining = allowance;
      const auto acquired = budget.acquired;
      const auto released = budget.released;
      {
        auto result = [&]() {
          if constexpr (Mutation == DeepMutation::kInsert) {
            return snapshot.try_insert({kNewKey, 30});
          } else if constexpr (Mutation == DeepMutation::kErase) {
            return snapshot.try_erase(0);
          } else {
            return snapshot.try_update(0, [](int& value) noexcept { value = 99; });
          }
        }();
        if (const auto* updated = std::get_if<std::pair<Map, bool>>(&result); updated != nullptr) {
          EXPECT_THAT(updated->second, Eq(true));
          if constexpr (Mutation == DeepMutation::kInsert) {
            EXPECT_THAT(updated->first.at(0), Eq(10));
            EXPECT_THAT(&updated->first.at(0), Eq(original));
            EXPECT_THAT(updated->first.at(kNewKey), Eq(30));
            EXPECT_THAT(updated->first.size(), Eq(3));
          } else if constexpr (Mutation == DeepMutation::kErase) {
            EXPECT_THAT(updated->first.count(0), Eq(0));
            EXPECT_THAT(updated->first.size(), Eq(1));
          } else {
            EXPECT_THAT(updated->first.at(0), Eq(99));
            EXPECT_THAT(&updated->first.at(0), Ne(original));
            EXPECT_THAT(updated->first.size(), Eq(2));
          }
          EXPECT_THAT(updated->first.at(kHighBit), Eq(20));
          EXPECT_THAT(&updated->first.at(kHighBit), Eq(sibling));
          succeeded = true;
        } else {
          EXPECT_THAT(result, VariantWith<HamtError>(HamtError::kAllocationExhausted));
          ++failures;
        }
      }
      EXPECT_THAT(budget.acquired - acquired, Eq(budget.released - released));
      EXPECT_THAT(snapshot.at(0), Eq(10));
      EXPECT_THAT(snapshot.at(kHighBit), Eq(20));
      EXPECT_THAT(snapshot.count(kNewKey), Eq(0));
      EXPECT_THAT(&snapshot.at(0), Eq(original));
      EXPECT_THAT(&snapshot.at(kHighBit), Eq(sibling));
      if (succeeded) {
        break;
      }
    }
    EXPECT_THAT(succeeded, Eq(true));
    constexpr auto kMinFailures = Mutation == DeepMutation::kErase ? std::size_t{0} : std::size_t{1};
    EXPECT_THAT(failures, Gt(kMinFailures));
  }
  EXPECT_THAT(budget.acquired, Eq(budget.released));
}

TEST_F(HamtNodeMapTest, FourBitDeepPersistentUpdateRollsBackEveryAllocationBoundary) {
  CheckDeepMutationFailure<4>();
}

TEST_F(HamtNodeMapTest, FiveBitDeepPersistentUpdateRollsBackEveryAllocationBoundary) {
  CheckDeepMutationFailure<5>();
}

TEST_F(HamtNodeMapTest, SixBitDeepPersistentUpdateRollsBackEveryAllocationBoundary) {
  CheckDeepMutationFailure<6>();
}

TEST_F(HamtNodeMapTest, SevenBitDeepPersistentUpdateRollsBackEveryAllocationBoundary) {
  CheckDeepMutationFailure<7>();
}

TEST_F(HamtNodeMapTest, FourBitDeepPersistentInsertionRollsBackEveryAllocationBoundary) {
  CheckDeepMutationFailure<4, DeepMutation::kInsert>();
}

TEST_F(HamtNodeMapTest, FiveBitDeepPersistentInsertionRollsBackEveryAllocationBoundary) {
  CheckDeepMutationFailure<5, DeepMutation::kInsert>();
}

TEST_F(HamtNodeMapTest, SixBitDeepPersistentInsertionRollsBackEveryAllocationBoundary) {
  CheckDeepMutationFailure<6, DeepMutation::kInsert>();
}

TEST_F(HamtNodeMapTest, SevenBitDeepPersistentInsertionRollsBackEveryAllocationBoundary) {
  CheckDeepMutationFailure<7, DeepMutation::kInsert>();
}

TEST_F(HamtNodeMapTest, FourBitDeepPersistentErasureRollsBackEveryAllocationBoundary) {
  CheckDeepMutationFailure<4, DeepMutation::kErase>();
}

TEST_F(HamtNodeMapTest, FiveBitDeepPersistentErasureRollsBackEveryAllocationBoundary) {
  CheckDeepMutationFailure<5, DeepMutation::kErase>();
}

TEST_F(HamtNodeMapTest, SixBitDeepPersistentErasureRollsBackEveryAllocationBoundary) {
  CheckDeepMutationFailure<6, DeepMutation::kErase>();
}

TEST_F(HamtNodeMapTest, SevenBitDeepPersistentErasureRollsBackEveryAllocationBoundary) {
  CheckDeepMutationFailure<7, DeepMutation::kErase>();
}

TEST_F(HamtNodeMapTest, SnapshotsKeepTheirSourceAliveAfterOriginalContainersDisappear) {
  using Map = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget{.remaining = 2};
  std::optional<Map> retained;
  {
    auto created = Map::try_create(std::hash<int>{}, std::equal_to<>{}, budget);
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
    auto created = Map::try_create(std::hash<int>{}, std::equal_to<>{}, budget);
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

TEST_F(HamtNodeMapTest, SharedMutationFailuresPreservePersistentAndTransientValues) {
  using Map = HamtNodeMap<int, int, CollisionHash, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget{.remaining = 8};
  auto created = Map::try_create(CollisionHash{}, std::equal_to<>{}, budget);
  ASSERT_THAT(created.has_value(), Eq(true));
  auto edit = std::move(*created).transient();  // NOLINT(bugprone-unchecked-optional-access)
  EXPECT_THAT(edit.insert({1, 10}).second, Eq(true));
  EXPECT_THAT(edit.insert({2, 20}).second, Eq(true));
  const auto snapshot = std::move(edit).persistent();

  budget.remaining = 0;
  EXPECT_THAT(snapshot.try_erase(1), VariantWith<HamtError>(HamtError::kAllocationExhausted));
  EXPECT_THAT(snapshot.at(1), Eq(10));
  EXPECT_THAT(snapshot.at(2), Eq(20));

  auto child = snapshot.transient();
  EXPECT_THAT(child.try_find(1), VariantWith<HamtError>(HamtError::kAllocationExhausted));
  EXPECT_THAT(child.try_at(1), VariantWith<HamtError>(HamtError::kAllocationExhausted));
  EXPECT_THAT(child.try_get_or_insert(1), VariantWith<HamtError>(HamtError::kAllocationExhausted));
  EXPECT_THAT(child.try_insert({3, 30}), VariantWith<HamtError>(HamtError::kAllocationExhausted));
  EXPECT_THAT(child.try_erase(1), VariantWith<HamtError>(HamtError::kAllocationExhausted));
  EXPECT_THAT(std::as_const(child).at(1), Eq(10));
  EXPECT_THAT(std::as_const(child).at(2), Eq(20));
  EXPECT_THAT(snapshot.at(1), Eq(10));
  EXPECT_THAT(snapshot.at(2), Eq(20));
}

TEST_F(HamtNodeMapTest, UniqueBoundedMutationReportsPayloadAndMaximumSizeFailures) {
  using Map = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget{.remaining = 2};
  auto created = Map::try_create(std::hash<int>{}, std::equal_to<>{}, budget);
  ASSERT_THAT(created.has_value(), Eq(true));
  auto edit = std::move(*created).transient();  // NOLINT(bugprone-unchecked-optional-access)
  EXPECT_THAT(edit.insert({1, 10}).second, Eq(true));
  EXPECT_THAT(edit.try_get_or_insert(1), VariantWith<int*>(NotNull()));
  EXPECT_THAT(edit.try_get_or_insert(2), VariantWith<HamtError>(HamtError::kAllocationExhausted));
  EXPECT_THAT(edit.try_insert({2, 20}), VariantWith<HamtError>(HamtError::kAllocationExhausted));
  EXPECT_THAT(std::as_const(edit).at(1), Eq(10));

  using Limited = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{.maximum_size = 1}>;
  auto limited = Limited{}.transient();
  EXPECT_THAT(limited.insert({1, 10}).second, Eq(true));
  auto duplicate = limited.try_insert({1, 20});
  const auto* const unchanged = std::get_if<std::pair<Limited::transient_type::iterator, bool>>(&duplicate);
  ASSERT_THAT(unchanged, NotNull());
  EXPECT_THAT(unchanged->second, Eq(false));
  EXPECT_THAT(limited.try_insert({2, 20}), VariantWith<HamtError>(HamtError::kMaxSizeExceeded));
}

TEST_F(HamtNodeMapTest, MutablePreparationFailurePreservesValuesAndMissingLookupDoesNotAllocate) {
  using Map = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget{.remaining = 2};
  {
    auto created = Map::try_create(std::hash<int>{}, std::equal_to<>{}, budget);
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

TEST_F(HamtNodeMapTest, SwappingPreparedAndSharedTransientsPreservesPayloadIsolation) {
  using Map = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget{.remaining = 20};
  {
    auto created = Map::try_create(std::hash<int>{}, std::equal_to<>{}, budget);
    if (!created) {
      FAIL() << "source domain creation failed";
      return;
    }
    auto inserted = created->insert({42, 99});
    const auto& snapshot = inserted.first;
    auto prepared = snapshot.transient();
    auto result = prepared.try_begin();
    const auto* const position = std::get_if<Map::transient_type::iterator>(&result);
    ASSERT_THAT(position, NotNull());
    auto shared = snapshot.transient();
    prepared.swap(shared);
    budget.remaining = 0;
    EXPECT_THAT(prepared.try_begin(), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    auto private_result = shared.try_find(42);
    auto* const private_position = std::get_if<Map::transient_type::iterator>(&private_result);
    ASSERT_THAT(private_position, NotNull());
    ASSERT_THAT(*private_position, Ne(shared.end()));
    (*private_position)->second = 100;
    EXPECT_THAT(snapshot.at(42), Eq(99));
    EXPECT_THAT(std::as_const(prepared).at(42), Eq(99));
    EXPECT_THAT(std::as_const(shared).at(42), Eq(100));
    auto moved = std::move(shared);
    auto published = std::move(moved).persistent();
    // NOLINTNEXTLINE(bugprone-use-after-move): verifies the documented moved-from transient state.
    EXPECT_THAT(moved, IsEmpty());
    EXPECT_THAT(published.at(42), Eq(100));
    auto child = published.transient();
    EXPECT_THAT(child.try_begin(), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    budget.remaining = 20;
    EXPECT_THAT(moved.insert({7, 8}).second, Eq(true));
    EXPECT_THAT(std::as_const(moved).at(7), Eq(8));
    EXPECT_THAT(published.contains(7), Eq(false));
  }
  EXPECT_THAT(budget.acquired, Eq(budget.released));
}

TEST_F(HamtNodeMapTest, RecoverableMutableAccessFailuresPreserveBothSharedEntries) {
  using Map = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget{.remaining = 20};
  {
    auto created = Map::try_create(std::hash<int>{}, std::equal_to<>{}, budget);
    if (!created) {
      FAIL() << "source domain creation failed";
      return;
    }
    auto first = created->insert({42, 99});
    auto second = first.first.insert({7, 8});
    const auto& snapshot = second.first;
    const auto* const first_address = &snapshot.at(42);
    const auto* const second_address = &snapshot.at(7);
    auto edit = snapshot.transient();
    budget.remaining = 0;
    EXPECT_THAT(edit.try_find(42), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    EXPECT_THAT(edit.try_at(42), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    EXPECT_THAT(edit.try_get_or_insert(42), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    EXPECT_THAT(edit.try_get_or_insert(100), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    const Map::value_type entry(100, 5);
    EXPECT_THAT(edit.try_insert(entry), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    EXPECT_THAT(edit.try_erase(42), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    EXPECT_THAT(edit, SizeIs(2));
    EXPECT_THAT(std::as_const(edit).at(42), Eq(99));
    EXPECT_THAT(std::as_const(edit).at(7), Eq(8));
    EXPECT_THAT(&std::as_const(edit).at(42), Eq(first_address));
    EXPECT_THAT(&std::as_const(edit).at(7), Eq(second_address));
    EXPECT_THAT(snapshot.at(42), Eq(99));
    EXPECT_THAT(snapshot.at(7), Eq(8));
  }
  EXPECT_THAT(budget.acquired, Eq(budget.released));
}

TEST_F(HamtNodeMapTest, MoveAssignmentDoesNotReuseTheReplacedMapsPayloadProof) {
  using Map = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget{.remaining = 20};
  {
    auto created = Map::try_create(std::hash<int>{}, std::equal_to<>{}, budget);
    if (!created) {
      FAIL() << "source domain creation failed";
      return;
    }
    auto inserted = created->insert({42, 99});
    const auto& snapshot = inserted.first;
    auto target = snapshot.transient();
    auto prepared = target.try_begin();
    ASSERT_THAT(std::get_if<Map::transient_type::iterator>(&prepared), NotNull());
    auto shared = snapshot.transient();
    target = std::move(shared);
    // NOLINTNEXTLINE(bugprone-use-after-move): verifies the documented moved-from transient state.
    EXPECT_THAT(shared, IsEmpty());
    budget.remaining = 0;
    EXPECT_THAT(target.try_find(42), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    EXPECT_THAT(snapshot.at(42), Eq(99));
    EXPECT_THAT(std::as_const(target).at(42), Eq(99));
    budget.remaining = 20;
    auto found = target.try_find(42);
    auto* const position = std::get_if<Map::transient_type::iterator>(&found);
    ASSERT_THAT(position, NotNull());
    ASSERT_THAT(*position, Ne(target.end()));
    (*position)->second = 100;
    EXPECT_THAT(snapshot.at(42), Eq(99));
    EXPECT_THAT(std::as_const(target).at(42), Eq(100));
  }
  EXPECT_THAT(budget.acquired, Eq(budget.released));
}

TEST_F(HamtNodeMapTest, EmptyMutableInsertionReportsPayloadAllocationFailure) {
  using Map = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget;
  {
    auto created = Map::try_create(std::hash<int>{}, std::equal_to<>{}, budget);
    if (!created) {
      FAIL() << "source domain creation failed";
      return;
    }
    auto edit = created->transient();
    const Map::value_type entry(100, 5);
    EXPECT_THAT(edit.try_insert(entry), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    EXPECT_THAT(edit.try_get_or_insert(100), VariantWith<HamtError>(HamtError::kAllocationExhausted));
    EXPECT_THAT(edit, IsEmpty());
    EXPECT_THAT(budget.acquired, Eq(0));
  }
  EXPECT_THAT(budget.acquired, Eq(budget.released));
}

TEST_F(HamtNodeMapTest, ConstTransientTraversalAndLookupDoNotDetachSharedPayloads) {
  using Map = HamtNodeMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  AllocationBudget budget{.remaining = 20};
  {
    auto created = Map::try_create(std::hash<int>{}, std::equal_to<>{}, budget);
    if (!created) {
      FAIL() << "source domain creation failed";
      return;
    }
    auto first = created->insert({42, 99});
    auto second = first.first.insert({7, 8});
    const auto& snapshot = second.first;
    auto edit = snapshot.transient();
    const auto& read = std::as_const(edit);
    budget.remaining = 0;
    const auto acquired = budget.acquired;
    int sum = 0;
    for (const auto& entry : read) {
      sum += entry.second;
    }
    EXPECT_THAT(sum, Eq(107));
    EXPECT_THAT(read, UnorderedElementsAre(Pair(42, 99), Pair(7, 8)));
    const auto found = read.find(42);
    ASSERT_THAT(found, Ne(read.cend()));
    EXPECT_THAT(found->second, Eq(99));
    EXPECT_THAT(read.find(100), Eq(read.cend()));
    EXPECT_THAT(read.contains(42), Eq(true));
    EXPECT_THAT(read.contains(100), Eq(false));
    EXPECT_THAT(&read.at(42), Eq(&snapshot.at(42)));
    EXPECT_THAT(&read.at(7), Eq(&snapshot.at(7)));
    EXPECT_THAT(budget.acquired, Eq(acquired));
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
  auto found = edit.find(std::int16_t{42});
  found->second = 100;
  EXPECT_THAT(std::as_const(edit).at(short{42}), Eq(100));
  EXPECT_THAT(inserted.first.at(short{42}), Eq(99));
}

TEST_F(HamtNodeMapTest, ConstTransientLookupAndTraversalSupportMoveOnlyMappedValues) {
  auto inserted = MoveOnlyMap{}.insert({42, std::make_unique<int>(99)});
  const auto snapshot = std::move(inserted.first);
  auto edit = snapshot.transient();
  const auto& read = std::as_const(edit);
  const auto found = read.find(std::int16_t{42});
  ASSERT_THAT(found, Ne(read.cend()));
  ASSERT_THAT(found->second, NotNull());
  EXPECT_THAT(*found->second, Eq(99));
  EXPECT_THAT(found->second.get(), Eq(snapshot.at(42).get()));
  EXPECT_THAT(read.find(short{7}), Eq(read.cend()));
  EXPECT_THAT(read.contains(short{42}), Eq(true));
  EXPECT_THAT(read.contains(short{7}), Eq(false));
  EXPECT_THAT(read, SizeIs(1));
  auto iterator = edit.cbegin();
  ASSERT_THAT(iterator, Ne(edit.cend()));
  EXPECT_THAT(iterator->first, Eq(42));
  EXPECT_THAT(iterator->second.get(), Eq(snapshot.at(42).get()));
  ++iterator;
  EXPECT_THAT(iterator, Eq(edit.cend()));
}

TEST_F(HamtNodeMapTest, PersistentMappedEditDetachesPayloadAndPreservesSnapshot) {
  using Map = HamtNodeMap<int, int>;
  const Map empty;
  auto inserted = empty.insert({42, 99});
  const Map original = std::move(inserted.first);
  auto result = original.try_update(42, [](int& value) noexcept { value = 100; });
  auto* const updated = std::get_if<std::pair<Map, bool>>(&result);
  ASSERT_THAT(updated, NotNull());
  EXPECT_THAT(updated->second, Eq(true));
  EXPECT_THAT(updated->first.at(42), Eq(100));
  EXPECT_THAT(original.at(42), Eq(99));
  EXPECT_THAT(empty.empty(), Eq(true));
}

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
  const Bounded empty;
  EXPECT_THAT(empty.try_insert({1, 10}), VariantWith<HamtError>(HamtError::kAllocationExhausted));
  EXPECT_THAT(empty.empty(), Eq(true));
  auto clone = std::move(inserted.first).try_clone_to<mbo::memory::InlineBlockSource<1>>();
  EXPECT_THAT(clone.has_value(), Eq(false));
  EXPECT_THAT(inserted.first.at(1), Eq(10));
}

TEST_F(HamtNodeMapTest, TransientAccessAndIterationPreservePublishedSnapshots) {
  using Map = HamtNodeMap<int, int>;
  const Map empty;
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
  // NOLINTNEXTLINE(bugprone-use-after-move): verifies the documented moved-from transient state.
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

TEST_F(HamtNodeMapTest, MissingRequiredAccessTerminatesForPersistentAndTransientMaps) {
  using Map = HamtNodeMap<int, int>;
  const Map empty;
  EXPECT_DEATH(static_cast<void>(empty.at(42)), "");
  auto edit = empty.transient();
  EXPECT_DEATH(static_cast<void>(edit.at(42)), "");
}

TEST_F(HamtNodeMapTest, DefaultInsertionClearSwapAndConsumingCloneLeaveReusableContainers) {
  using Map = HamtNodeMap<int, int>;
  auto edit = Map{}.transient();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): Tests the public subscript API.
  edit.operator[](42) = 99;
  EXPECT_THAT(edit.at(42), Eq(99));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): Tests default insertion.
  EXPECT_THAT(edit.operator[](7), Eq(0));
  auto other = Map{}.transient();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): Tests the public subscript API.
  other.operator[](1) = 10;
  edit.swap(other);
  EXPECT_THAT(edit.at(1), Eq(10));
  EXPECT_THAT(other.at(42), Eq(99));
  auto cloned = std::move(other).try_clone_to<mbo::memory::NewDeleteBlockSource>();
  ASSERT_THAT(cloned.has_value(), Eq(true));
  const auto copy = std::move(cloned).value_or(Map{});
  EXPECT_THAT(copy.at(42), Eq(99));
  EXPECT_THAT(copy.at(7), Eq(0));
  // NOLINTNEXTLINE(bugprone-use-after-move): verifies the documented moved-from transient state.
  EXPECT_THAT(other.empty(), Eq(true));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): Verifies moved-from reuse.
  other.operator[](2) = 20;
  EXPECT_THAT(other.at(2), Eq(20));
  other.clear();
  EXPECT_THAT(other.empty(), Eq(true));
}
}  // namespace
}  // namespace mbo::container
