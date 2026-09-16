// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/hamt_flat_map.h"

#include <cstddef>
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

  void Release(mbo::memory::MemoryBlock block) noexcept { mbo::memory::NewDeleteBlockSource::Release(block); }

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
  budget = 0;
  EXPECT_THAT(shared.try_erase(1), VariantWith<HamtError>(Eq(HamtError::kAllocationExhausted)));
  EXPECT_THAT(shared, SizeIs(2));
  EXPECT_THAT(snapshot, SizeIs(2));
  EXPECT_THAT(shared.at(1), Eq(10));
  EXPECT_THAT(shared.at(2), Eq(20));
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

struct SetValue final {
  void operator()(int& mapped) const noexcept { mapped = value; }

  int value = 0;
};

struct ThrowingEditor final {
  void operator()(int&) const noexcept(false) {}
};

struct ReturningEditor final {
  bool operator()(int&) const noexcept { return true; }
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
  Map empty;
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
  EXPECT_THAT(edit.empty(), Eq(true));
  EXPECT_THAT(edit.insert(Map::value_type(2, 20)).second, Eq(true));
  EXPECT_THAT(edit.erase(2), Eq(1));
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
    EXPECT_THAT(shared.at(1), Eq(10));
    EXPECT_THAT(snapshot.at(1), Eq(10));
  }
  auto unique = std::move(snapshot).transient();
  EXPECT_THAT(unique.try_update(1, SetValue{.value = 99}), VariantWith<bool>(Eq(true)));
  EXPECT_THAT(unique.at(1), Eq(99));
}
}  // namespace
}  // namespace mbo::container
