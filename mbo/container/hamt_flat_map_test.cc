// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/hamt_flat_map.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/hamt_options.h"
#include "mbo/memory/block_source.h"

namespace mbo::container {
namespace {
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::NotNull;
using ::testing::SizeIs;
using ::testing::VariantWith;
using Map = HamtFlatMap<int, int>;

struct HamtFlatMapTest : ::testing::Test {};

struct BudgetSource final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  explicit BudgetSource(std::size_t& budget) noexcept : budget(budget) {}

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    if (budget == 0) {
      return std::nullopt;
    }
    --budget;
    return mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
  }

  static void Release(mbo::memory::MemoryBlock block) noexcept { mbo::memory::NewDeleteBlockSource::Release(block); }

  std::size_t& budget;
};

TEST_F(HamtFlatMapTest, SharedTransientErasureFailurePreservesBothMappedValues) {
  using BudgetMap = HamtFlatMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, BudgetSource>;
  std::size_t budget = 8;
  auto created = BudgetMap::TryCreate(std::hash<int>{}, std::equal_to<>{}, budget);
  if (!created) {
    FAIL() << "map creation failed";
    return;
  }
  auto edit = created->transient();
  EXPECT_THAT(edit.insert(BudgetMap::value_type(1, 10)).second, Eq(true));
  EXPECT_THAT(edit.insert(BudgetMap::value_type(2, 20)).second, Eq(true));
  const auto snapshot = std::move(edit).persistent();
  auto shared = snapshot.transient();
  // BudgetSource observes this reference through type-erased tree storage.
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  budget = 0;
  EXPECT_THAT(shared.try_erase(1), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(shared, SizeIs(2));
  EXPECT_THAT(snapshot, SizeIs(2));
  EXPECT_THAT(std::as_const(shared).at(1), Eq(10));
  EXPECT_THAT(std::as_const(shared).at(2), Eq(20));
  EXPECT_THAT(snapshot.at(1), Eq(10));
  EXPECT_THAT(snapshot.at(2), Eq(20));
}

struct CollisionHash final {
  constexpr std::uint64_t operator()(int) const noexcept { return 7; }
};

struct CopyObservedMapped final {
  explicit CopyObservedMapped(int& copies) noexcept : copies(&copies) {}

  CopyObservedMapped(const CopyObservedMapped& other) noexcept : copies(other.copies) { ++*copies; }

  CopyObservedMapped& operator=(const CopyObservedMapped&) noexcept = default;
  CopyObservedMapped(CopyObservedMapped&&) noexcept = default;
  CopyObservedMapped& operator=(CopyObservedMapped&&) noexcept = default;
  ~CopyObservedMapped() = default;

  int* copies;
};

TEST_F(HamtFlatMapTest, UniqueMutableLookupDoesNotCopyMappedValues) {
  using ObservedMap = HamtFlatMap<int, CopyObservedMapped>;
  int copies = 0;
  ObservedMap empty;
  auto edit = std::move(empty).transient();
  EXPECT_THAT(edit.insert(ObservedMap::value_type(1, CopyObservedMapped(copies))).second, Eq(true));
  copies = 0;
  auto result = edit.try_find(1);
  const auto* const position = std::get_if<ObservedMap::transient_type::iterator>(&result);
  ASSERT_THAT(position, NotNull());
  EXPECT_THAT(*position == edit.end(), Eq(false));
  EXPECT_THAT(copies, Eq(0));
}

TEST_F(HamtFlatMapTest, MutableCollisionIteratorsAdvanceWithoutChangingSnapshots) {
  using CollisionMap = HamtFlatMap<int, int, CollisionHash>;
  CollisionMap empty;
  auto builder = std::move(empty).transient();
  EXPECT_THAT(builder.insert(CollisionMap::value_type(1, 10)).second, Eq(true));
  EXPECT_THAT(builder.insert(CollisionMap::value_type(2, 20)).second, Eq(true));
  const auto snapshot = std::move(builder).persistent();
  auto edit = snapshot.transient();
  auto position = edit.find(1);
  ASSERT_THAT(position == edit.end(), Eq(false));
  auto copied = position;
  position->second = 100;
  EXPECT_THAT(copied->second, Eq(100));
  ++position;
  ASSERT_THAT(position == edit.end(), Eq(false));
  EXPECT_THAT(position->first, Eq(2));
  position->second = 200;
  ++copied;
  EXPECT_THAT(position == copied, Eq(true));
  ++position;
  EXPECT_THAT(position == edit.end(), Eq(true));
  EXPECT_THAT(snapshot.at(1), Eq(10));
  EXPECT_THAT(snapshot.at(2), Eq(20));
}

TEST_F(HamtFlatMapTest, ExhaustedNodeStorageReportsInsertionFailureWithoutPublishingEntries) {
  using BoundedMap =
      HamtFlatMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, mbo::memory::InlineBlockSource<1>>;
  auto created = BoundedMap::TryCreate();
  if (!created) {
    FAIL() << "map creation failed";
    return;
  }
  EXPECT_THAT(
      created->try_insert(BoundedMap::value_type(1, 10)), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  auto edit = created->transient();
  EXPECT_THAT(
      edit.try_insert(BoundedMap::value_type(1, 10)), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(edit.try_get_or_insert(1), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(*created, IsEmpty());
  EXPECT_THAT(edit, IsEmpty());
  EXPECT_DEATH(static_cast<void>(created->insert(BoundedMap::value_type(1, 10))), "");
}

TEST_F(HamtFlatMapTest, SwapAndConstRangesPreserveTheVisibleKeyAndMappedValue) {
  Map empty;
  auto [one, inserted] = empty.insert(Map::value_type(1, 10));
  EXPECT_THAT(inserted, Eq(true));
  swap(empty, one);
  EXPECT_THAT(empty, SizeIs(1));
  EXPECT_THAT(one, IsEmpty());
  EXPECT_THAT(empty.count(1), Eq(1));
  EXPECT_THAT(empty.count(2), Eq(0));
  EXPECT_THAT(empty.cbegin() == empty.begin(), Eq(true));
  EXPECT_THAT(empty.cend() == empty.end(), Eq(true));
  EXPECT_THAT(empty.at(1), Eq(10));
  EXPECT_DEATH(static_cast<void>(empty.at(2)), "");
}

TEST_F(HamtFlatMapTest, MutableIteratorsKeepKeysConstAndDetachFromPersistentSnapshots) {
  const Map empty;
  auto [snapshot, inserted] = empty.insert(Map::value_type(1, 10));
  EXPECT_THAT(inserted, Eq(true));
  auto edit = snapshot.transient();
  auto position = edit.find(1);
  ASSERT_THAT(position == edit.end(), Eq(false));
  static_assert(std::is_const_v<std::remove_reference_t<decltype((position->first))>>);
  static_assert(!std::is_const_v<std::remove_reference_t<decltype((position->second))>>);
  Map::iterator immutable = position;
  EXPECT_THAT(immutable == position, Eq(true));
  position->second = 99;
  EXPECT_THAT(snapshot.at(1), Eq(10));
  EXPECT_THAT(std::as_const(edit).at(1), Eq(99));
  auto [duplicate, changed] = edit.insert(Map::value_type(1, 77));
  EXPECT_THAT(changed, Eq(false));
  duplicate->second = 55;
  EXPECT_THAT(std::as_const(edit).at(1), Eq(55));
  EXPECT_THAT(edit.cbegin() == edit.begin(), Eq(true));
  const auto& borrowed = *edit.cbegin();
  auto alias_result = edit.try_insert(borrowed);
  const auto* const alias = std::get_if<std::pair<Map::transient_type::iterator, bool>>(&alias_result);
  ASSERT_THAT(alias, NotNull());
  EXPECT_THAT(alias->second, Eq(false));
  EXPECT_THAT(alias->first->second, Eq(55));
}

TEST_F(HamtFlatMapTest, MaximumSizeFailurePrecedesSharedIteratorAllocation) {
  constexpr HamtOptions kOneEntry{.fragment_bits = 5, .maximum_size = 1};
  using BoundedMap =
      HamtFlatMap<int, int, std::hash<int>, std::equal_to<>, kOneEntry, mbo::memory::InlineBlockSource<512>>;
  BoundedMap empty;
  auto builder = std::move(empty).transient();
  EXPECT_THAT(builder.insert(BoundedMap::value_type(1, 10)).second, Eq(true));
  const auto snapshot = std::move(builder).persistent();
  auto edit = snapshot.transient();
  EXPECT_THAT(edit.try_insert(BoundedMap::value_type(2, 20)), VariantWith<HamtError>(Eq(HamtError::kMaxSizeExceeded)));
  EXPECT_THAT(
      edit.try_insert(BoundedMap::value_type(1, 99)), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(snapshot.at(1), Eq(10));
  EXPECT_THAT(edit.size(), Eq(1));
}

TEST_F(HamtFlatMapTest, PublicCloneChangesSourceAndConsumesOnlyOnSuccess) {
  const Map empty;
  auto [one, inserted] = empty.insert(Map::value_type(1, 10));
  EXPECT_THAT(inserted, Eq(true));
  auto copied = one.try_clone_to<mbo::memory::InlineBlockSource<1'024>>();
  if (!copied) {
    FAIL() << "cross-source clone failed";
    return;
  }
  EXPECT_THAT(copied->at(1), Eq(10));
  EXPECT_THAT(std::addressof(*copied->find(1)) == std::addressof(*one.find(1)), Eq(false));
  auto failed = std::move(one).try_clone_to<mbo::memory::InlineBlockSource<1>>();
  EXPECT_THAT(failed.has_value(), Eq(false));
  // A failed consuming clone must preserve the source persistent value.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_THAT(one.at(1), Eq(10));
  auto edit = one.transient();
  auto failed_transient = std::move(edit).try_clone_to<mbo::memory::InlineBlockSource<1>>();
  EXPECT_THAT(failed_transient.has_value(), Eq(false));
  // A failed consuming clone must preserve the source transient.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_THAT(std::as_const(edit).at(1), Eq(10));
  // Reuse after the specified failed move is intentional.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  auto consumed_transient = std::move(edit).try_clone_to<mbo::memory::NewDeleteBlockSource>();
  EXPECT_THAT(consumed_transient.has_value(), Eq(true));
  // A successful consuming clone leaves the source transient empty.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_THAT(edit.empty(), Eq(true));
  // Reuse after the specified failed move above is intentional.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  auto consumed = std::move(one).try_clone_to<mbo::memory::NewDeleteBlockSource>();
  EXPECT_THAT(consumed.has_value(), Eq(true));
  // A successful consuming clone leaves the source persistent value empty.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_THAT(one, IsEmpty());
}

struct SetValue final {
  void operator()(int& mapped) const noexcept { mapped = value; }

  int value = 0;
};

struct ThrowingEditor final {
  void operator()(int& /*unused*/) const noexcept(false) {}
};

struct ReturningEditor final {
  bool operator()(int& /*unused*/) const noexcept { return true; }
};

template<typename Editor>
concept SupportsEditor = requires(const Map& map, const Editor& editor) { map.try_update(1, editor); };
static_assert(SupportsEditor<SetValue>);
static_assert(!SupportsEditor<ThrowingEditor>);
static_assert(!SupportsEditor<ReturningEditor>);

static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<Map::value_type>().first)>>);

TEST_F(HamtFlatMapTest, PersistentInsertAndDuplicatePreserveOriginalMappedValues) {
  const Map empty;
  auto [one, inserted] = empty.insert(Map::value_type(1, 10));
  EXPECT_THAT(inserted, Eq(true));
  EXPECT_THAT(empty, IsEmpty());
  EXPECT_THAT(one.at(1), Eq(10));
  auto [duplicate, changed] = one.insert(Map::value_type(1, 99));
  EXPECT_THAT(changed, Eq(false));
  EXPECT_THAT(duplicate.at(1), Eq(10));
  EXPECT_THAT(one.at(1), Eq(10));
}

TEST_F(HamtFlatMapTest, MappedEditorCannotChangeKeysAndPreservesSnapshots) {
  const Map empty;
  auto [one, inserted] = empty.insert(Map::value_type(1, 10));
  EXPECT_THAT(inserted, Eq(true));
  auto edit = one.transient();
  EXPECT_THAT(edit.try_update(1, SetValue{.value = 99}), VariantWith<bool>(Eq(true)));
  EXPECT_THAT(edit.at(1), Eq(99));
  EXPECT_THAT(one.at(1), Eq(10));
  EXPECT_THAT(edit.try_update(2, SetValue{.value = 77}), VariantWith<bool>(Eq(false)));
  auto snapshot = std::move(edit).persistent();
  auto updated = snapshot.try_update(1, SetValue{.value = 42});
  auto* const result = std::get_if<std::pair<Map, bool>>(&updated);
  ASSERT_THAT(result, NotNull());
  EXPECT_THAT(result->second, Eq(true));
  EXPECT_THAT(result->first.at(1), Eq(42));
  EXPECT_THAT(snapshot.at(1), Eq(99));
  // NOLINTNEXTLINE(bugprone-use-after-move): verifies the documented moved-from transient state.
  EXPECT_THAT(edit.empty(), Eq(true));
  EXPECT_THAT(edit.insert(Map::value_type(2, 20)).second, Eq(true));
  EXPECT_THAT(edit.erase(2), Eq(1));
}

TEST_F(HamtFlatMapTest, MutableAccessDetachesSnapshotsAndSubscriptInsertsZeroInitializedValues) {
  const Map empty;
  auto [one, inserted] = empty.insert(Map::value_type(1, 10));
  EXPECT_THAT(inserted, Eq(true));
  auto edit = one.transient();
  edit.at(1) = 99;
  EXPECT_THAT(one.at(1), Eq(10));
  EXPECT_THAT(std::as_const(edit).at(1), Eq(99));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): Tests the public subscript API.
  int& inserted_value = edit.operator[](2);
  EXPECT_THAT(inserted_value, Eq(0));
  inserted_value = 20;
  EXPECT_THAT(edit.size(), Eq(2));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): Covers existing-key subscript.
  const int& existing_value = edit.operator[](2);
  EXPECT_THAT(existing_value, Eq(20));
  EXPECT_THAT(edit.try_at(3), VariantWith<int*>(Eq(nullptr)));
  auto snapshot = std::move(edit).persistent();
  EXPECT_THAT(snapshot.at(1), Eq(99));
  EXPECT_THAT(snapshot.at(2), Eq(20));
}

TEST_F(HamtFlatMapTest, RecoverableMutableAccessPreservesSharedValuesWhenAllocationFails) {
  using BoundedMap =
      HamtFlatMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, mbo::memory::InlineBlockSource<512>>;
  BoundedMap empty;
  auto edit = std::move(empty).transient();
  EXPECT_THAT(edit.insert(BoundedMap::value_type(1, 10)).second, Eq(true));
  auto snapshot = std::move(edit).persistent();
  auto shared = snapshot.transient();
  EXPECT_THAT(shared.try_begin(), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(shared.try_find(1), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  auto missing = shared.try_find(2);
  const auto* const missing_position = std::get_if<BoundedMap::transient_type::iterator>(&missing);
  ASSERT_THAT(missing_position, NotNull());
  EXPECT_THAT(*missing_position == shared.end(), Eq(true));
  EXPECT_THAT(
      shared.try_insert(BoundedMap::value_type(2, 20)), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(
      shared.try_insert(BoundedMap::value_type(1, 99)), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(std::as_const(shared).find(1) == shared.cend(), Eq(false));
  EXPECT_THAT(shared.try_at(1), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(shared.try_get_or_insert(2), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(snapshot.at(1), Eq(10));
  EXPECT_THAT(std::as_const(shared).at(1), Eq(10));
  EXPECT_THAT(shared.size(), Eq(1));
}

TEST_F(HamtFlatMapTest, UniqueMappedUpdateWorksWhenSharedPathCopyCannotAllocate) {
  using BoundedMap =
      HamtFlatMap<int, int, std::hash<int>, std::equal_to<>, HamtOptions{}, mbo::memory::InlineBlockSource<512>>;
  BoundedMap empty;
  auto edit = std::move(empty).transient();
  EXPECT_THAT(edit.insert(BoundedMap::value_type(1, 10)).second, Eq(true));
  auto snapshot = std::move(edit).persistent();
  EXPECT_THAT(
      snapshot.try_update(1, SetValue{.value = 99}), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(snapshot.at(1), Eq(10));
  {
    auto shared = snapshot.transient();
    EXPECT_THAT(
        shared.try_update(1, SetValue{.value = 77}), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
    EXPECT_THAT(std::as_const(shared).at(1), Eq(10));
    EXPECT_THAT(snapshot.at(1), Eq(10));
  }
  auto unique = std::move(snapshot).transient();
  EXPECT_THAT(unique.try_update(1, SetValue{.value = 99}), VariantWith<bool>(Eq(true)));
  EXPECT_THAT(unique.at(1), Eq(99));
}
}  // namespace
}  // namespace mbo::container
