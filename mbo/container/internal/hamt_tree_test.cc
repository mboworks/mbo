// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_tree.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

struct Entry final {
  int key;
  int value;
  friend constexpr bool operator==(const Entry&, const Entry&) = default;
};

struct SeedHash final {
  std::uint64_t seed = 13;
  std::uint64_t mask = std::numeric_limits<std::uint64_t>::max();

  constexpr std::uint64_t operator()(std::int64_t key) const noexcept {
    return (static_cast<std::uint64_t>(key) & mask) ^ seed;
  }
};

struct KeyOf final {
  constexpr const int& operator()(const Entry& entry) const noexcept { return entry.key; }
};

struct Equal final {
  int tag = 7;

  constexpr bool operator()(int lhs, std::int64_t rhs) const noexcept { return lhs == rhs; }
};

struct BudgetSource final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    if (remaining == 0) {
      return std::nullopt;
    }
    const auto block = mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
    if (block) {
      --remaining;
      ++acquired;
    }
    return block;
  }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    ++released;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }

  std::size_t remaining = 64;
  std::size_t acquired = 0;
  std::size_t released = 0;
};

constexpr HamtOptions kOptions{.fragment_bits = 5, .maximum_size = 2};
using Tree = HamtTree<kOptions, Entry, SeedHash, KeyOf, Equal, BudgetSource>;

::testing::Matcher<HamtMutationResult> MutationIs(bool changed, std::optional<HamtError> error = {}) {
  return AllOf(
      Field("changed", &HamtMutationResult::changed, Eq(changed)),
      Field("error", &HamtMutationResult::error, Eq(error)));
}

struct HamtTreeTest : ::testing::Test {
  BudgetSource source;
  Tree tree{source, SeedHash{}, KeyOf{}, Equal{}};
};

struct SetValue final {
  int value;
  int& calls;

  void operator()(Entry& entry) const noexcept {
    ++calls;
    entry.value = value;
  }
};

struct ThrowingEditor final {
  void operator()([[maybe_unused]] Entry& entry) const noexcept(false) {}
};

struct ReturningEditor final {
  bool operator()([[maybe_unused]] Entry& entry) const noexcept { return false; }
};

template<typename Editor>
concept SupportsUpdate = requires(Tree& tree, const Editor& editor) { tree.try_update(1, editor); };

static_assert(SupportsUpdate<SetValue>);
static_assert(!SupportsUpdate<ThrowingEditor>);
static_assert(!SupportsUpdate<ReturningEditor>);

TEST_F(HamtTreeTest, MissingUpdateOnAnEmptyTreeDoesNotInvokeTheEditorOrAllocate) {
  int calls = 0;
  EXPECT_THAT(tree.try_update(1, SetValue{.value = 99, .calls = calls}), MutationIs(false));
  EXPECT_THAT(calls, Eq(0));
  EXPECT_THAT(source.acquired, Eq(0));
  EXPECT_THAT(tree, IsEmpty());
}

TEST_F(HamtTreeTest, ASharedAncestorPreventsEditingAnOtherwiseUniqueDescendant) {
  constexpr HamtOptions kLargerOptions{.fragment_bits = 5, .maximum_size = 4};
  using LargerTree = HamtTree<kLargerOptions, Entry, SeedHash, KeyOf, Equal, BudgetSource>;
  LargerTree branch(source, SeedHash{}, KeyOf{}, Equal{});
  EXPECT_THAT(branch.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  EXPECT_THAT(branch.try_insert(Entry{.key = 98'305, .value = 20}), MutationIs(true));
  const LargerTree snapshot = branch;
  EXPECT_THAT(branch.try_insert(Entry{.key = 2, .value = 30}), MutationIs(true));
  int calls = 0;
  EXPECT_THAT(branch.try_update(98'305, SetValue{.value = 99, .calls = calls}), MutationIs(true));
  const Entry* const original = snapshot.Find(98'305);
  const Entry* const changed = branch.Find(98'305);
  ASSERT_THAT(original, NotNull());
  ASSERT_THAT(changed, NotNull());
  EXPECT_THAT(original->value, Eq(20));
  EXPECT_THAT(changed->value, Eq(99));
  EXPECT_THAT(calls, Eq(1));
}

TEST_F(HamtTreeTest, UniqueCollisionUpdatesPreserveSiblingValuesWithoutAllocation) {
  Tree collisions(source, SeedHash{.seed = 29, .mask = 0}, KeyOf{}, Equal{});
  EXPECT_THAT(collisions.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  EXPECT_THAT(collisions.try_insert(Entry{.key = 2, .value = 20}), MutationIs(true));
  const auto allocations = source.acquired;
  source.remaining = 0;
  int calls = 0;
  EXPECT_THAT(collisions.try_update(2, SetValue{.value = 99, .calls = calls}), MutationIs(true));
  EXPECT_THAT(collisions, UnorderedElementsAre(Entry{.key = 1, .value = 10}, Entry{.key = 2, .value = 99}));
  EXPECT_THAT(source.acquired, Eq(allocations));
  EXPECT_THAT(calls, Eq(1));
}

TEST_F(HamtTreeTest, UniqueDeepPathsUpdateWithoutAllocatingEvenWhenTheSourceIsExhausted) {
  EXPECT_THAT(tree.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  EXPECT_THAT(tree.try_insert(Entry{.key = 98'305, .value = 20}), MutationIs(true));
  const auto allocations = source.acquired;
  source.remaining = 0;
  int calls = 0;
  EXPECT_THAT(tree.try_update(98'305, SetValue{.value = 99, .calls = calls}), MutationIs(true));
  const Entry* const updated = tree.Find(98'305);
  ASSERT_THAT(updated, NotNull());
  EXPECT_THAT(updated->value, Eq(99));
  EXPECT_THAT(calls, Eq(1));
  EXPECT_THAT(source.acquired, Eq(allocations));
}

TEST_F(HamtTreeTest, SharedUpdatesCopyInsteadOfEditingTheOriginalSnapshot) {
  EXPECT_THAT(tree.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  const Tree snapshot = tree;
  int calls = 0;
  EXPECT_THAT(tree.try_update(1, SetValue{.value = 99, .calls = calls}), MutationIs(true));
  EXPECT_THAT(tree, ElementsAre(Entry{.key = 1, .value = 99}));
  EXPECT_THAT(snapshot, ElementsAre(Entry{.key = 1, .value = 10}));
  EXPECT_THAT(calls, Eq(1));
}

TEST_F(HamtTreeTest, FailedSharedUpdatesPreserveContainersButDoNotUndoEditorSideEffects) {
  EXPECT_THAT(tree.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  const Tree snapshot = tree;
  source.remaining = 0;
  int calls = 0;
  EXPECT_THAT(
      tree.try_update(1, SetValue{.value = 99, .calls = calls}), MutationIs(false, HamtError::kAllocationExhausted));
  EXPECT_THAT(tree, ElementsAre(Entry{.key = 1, .value = 10}));
  EXPECT_THAT(snapshot, ElementsAre(Entry{.key = 1, .value = 10}));
  EXPECT_THAT(calls, Eq(1));
  EXPECT_THAT(tree.try_update(2, SetValue{.value = 99, .calls = calls}), MutationIs(false));
  EXPECT_THAT(calls, Eq(1));
}

TEST_F(HamtTreeTest, EmptyTreeHasAnEmptyRangeAndNoMissingKeyMutations) {
  EXPECT_THAT(tree, IsEmpty());
  EXPECT_THAT(tree.begin(), Eq(tree.end()));
  EXPECT_THAT(Tree::max_size(), Eq(2));
  EXPECT_THAT(tree.contains(1), IsFalse());
  EXPECT_THAT(tree.find(1), Eq(tree.end()));
  EXPECT_THAT(tree.try_erase(1), MutationIs(false));
  EXPECT_THAT(tree.try_replace(Entry{.key = 1, .value = 10}), MutationIs(false));
  EXPECT_THAT(source.acquired, Eq(0));
}

TEST_F(HamtTreeTest, DuplicateInsertionStillSucceedsAtTheMaximumWithoutAllocating) {
  EXPECT_THAT(tree.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  EXPECT_THAT(tree.try_insert(Entry{.key = 2, .value = 20}), MutationIs(true));
  source.remaining = 0;
  EXPECT_THAT(tree.try_insert(Entry{.key = 1, .value = 99}), MutationIs(false));
  EXPECT_THAT(tree.try_insert(Entry{.key = 3, .value = 30}), MutationIs(false, HamtError::kMaxSizeExceeded));
  EXPECT_THAT(tree, SizeIs(2));
  const Entry* const original = tree.Find(std::int64_t{1});
  ASSERT_THAT(original, NotNull());
  EXPECT_THAT(original->value, Eq(10));
}

TEST_F(HamtTreeTest, SharedSnapshotsPreserveValuesAndStatefulHashingAcrossMutations) {
  EXPECT_THAT(tree.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  EXPECT_THAT(tree.try_insert(Entry{.key = 33, .value = 20}), MutationIs(true));
  const Tree snapshot = tree;
  EXPECT_THAT(tree.try_replace(Entry{.key = 33, .value = 99}), MutationIs(true));
  EXPECT_THAT(snapshot.hash_function().seed, Eq(13));
  EXPECT_THAT(snapshot.key_eq().tag, Eq(7));
  const Entry* const old = snapshot.Find(33);
  const Entry* const updated = tree.Find(33);
  ASSERT_THAT(old, NotNull());
  ASSERT_THAT(updated, NotNull());
  EXPECT_THAT(old->value, Eq(20));
  EXPECT_THAT(updated->value, Eq(99));
  EXPECT_THAT(tree.try_erase(1), MutationIs(true));
  EXPECT_THAT(tree, ElementsAre(Entry{.key = 33, .value = 99}));
  EXPECT_THAT(snapshot, UnorderedElementsAre(Entry{.key = 1, .value = 10}, Entry{.key = 33, .value = 20}));
  EXPECT_THAT(tree.try_erase(33), MutationIs(true));
  EXPECT_THAT(tree, IsEmpty());
  EXPECT_THAT(snapshot, SizeIs(2));
}

TEST_F(HamtTreeTest, ExhaustionPreservesTheContainerForInsertReplaceAndErase) {
  EXPECT_THAT(tree.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  source.remaining = 0;
  EXPECT_THAT(tree.try_insert(Entry{.key = 2, .value = 20}), MutationIs(false, HamtError::kAllocationExhausted));
  EXPECT_THAT(tree.try_replace(Entry{.key = 1, .value = 99}), MutationIs(false, HamtError::kAllocationExhausted));
  EXPECT_THAT(tree.try_erase(2), MutationIs(false));
  EXPECT_THAT(tree, ElementsAre(Entry{.key = 1, .value = 10}));
}

TEST_F(HamtTreeTest, FullHashCollisionsStillSupportHeterogeneousFindAndErase) {
  Tree collisions(source, SeedHash{.seed = 29, .mask = 0}, KeyOf{}, Equal{});
  EXPECT_THAT(collisions.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  EXPECT_THAT(collisions.try_insert(Entry{.key = 2, .value = 20}), MutationIs(true));
  const auto found = collisions.find(std::int64_t{2});
  ASSERT_THAT(found != collisions.end(), IsTrue());
  EXPECT_THAT(found->value, Eq(20));
  EXPECT_THAT(collisions.try_erase(std::int64_t{1}), MutationIs(true));
  EXPECT_THAT(collisions, ElementsAre(Entry{.key = 2, .value = 20}));
}

TEST_F(HamtTreeTest, MovesLeaveValidEmptyContainersWithTheSameHashSeed) {
  EXPECT_THAT(tree.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  Tree moved(std::move(tree));
  EXPECT_THAT(tree, IsEmpty());
  EXPECT_THAT(tree.hash_function().seed, Eq(13));
  EXPECT_THAT(tree.try_insert(Entry{.key = 2, .value = 20}), MutationIs(true));
  tree = std::move(moved);
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move): moved-from tree contract under test.
  EXPECT_THAT(moved, IsEmpty());
  EXPECT_THAT(tree, ElementsAre(Entry{.key = 1, .value = 10}));
}

TEST_F(HamtTreeTest, CopyAssignmentAndSwapPreserveThePairedHashAndAllocationDomain) {
  EXPECT_THAT(tree.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  BudgetSource other_source;
  Tree other(other_source, SeedHash{.seed = 77}, KeyOf{}, Equal{.tag = 42});
  EXPECT_THAT(other.try_insert(Entry{.key = 2, .value = 20}), MutationIs(true));
  swap(tree, other);
  EXPECT_THAT(tree.contains(2), IsTrue());
  EXPECT_THAT(tree.hash_function().seed, Eq(77));
  EXPECT_THAT(tree.key_eq().tag, Eq(42));
  other = tree;
  EXPECT_THAT(other.contains(2), IsTrue());
  EXPECT_THAT(source.acquired, Eq(source.released));
  tree.clear();
  other.clear();
  EXPECT_THAT(other_source.acquired, Eq(other_source.released));
}

TEST_F(HamtTreeTest, CloningToADifferentSourcePreservesStateAndIndependentLifetime) {
  EXPECT_THAT(tree.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  mbo::memory::NewDeleteBlockSource destination;
  using OtherTree = HamtTree<kOptions, Entry, SeedHash, KeyOf, Equal, mbo::memory::NewDeleteBlockSource>;
  auto cloned = tree.try_clone_to(destination).value_or(OtherTree(destination, SeedHash{.seed = 99}, KeyOf{}, Equal{}));
  EXPECT_THAT(cloned, ElementsAre(Entry{.key = 1, .value = 10}));
  EXPECT_THAT(cloned.hash_function().seed, Eq(13));
  tree.clear();
  EXPECT_THAT(cloned.contains(1), IsTrue());
  source.remaining = 0;
  EXPECT_THAT(cloned.try_clone_to(source), Eq(std::nullopt));
  EXPECT_THAT(tree.try_clone_to(source), Optional(IsEmpty()));
}

TEST_F(HamtTreeTest, FailedErasurePreservesBothEntriesAndTheVisibleSize) {
  EXPECT_THAT(tree.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  EXPECT_THAT(tree.try_insert(Entry{.key = 2, .value = 20}), MutationIs(true));
  source.remaining = 0;
  EXPECT_THAT(tree.try_erase(1), MutationIs(false, HamtError::kAllocationExhausted));
  EXPECT_THAT(tree, UnorderedElementsAre(Entry{.key = 1, .value = 10}, Entry{.key = 2, .value = 20}));
  EXPECT_THAT(tree, SizeIs(2));
}

struct IdentityKey final {
  constexpr int operator()(int key) const noexcept { return key; }
};

TEST_F(HamtTreeTest, TheSameCoreAlsoSupportsSetEntriesWithoutMappedStorage) {
  using SetTree = HamtTree<kOptions, int, SeedHash, IdentityKey, Equal, BudgetSource>;
  SetTree values(source, SeedHash{}, IdentityKey{}, Equal{});
  EXPECT_THAT(values.try_insert(1), MutationIs(true));
  EXPECT_THAT(values.try_insert(33), MutationIs(true));
  EXPECT_THAT(values.try_insert(1), MutationIs(false));
  EXPECT_THAT(values.contains(std::int64_t{33}), IsTrue());
  EXPECT_THAT(values, UnorderedElementsAre(1, 33));
  EXPECT_THAT(values.try_erase(std::int64_t{1}), MutationIs(true));
  EXPECT_THAT(values, ElementsAre(33));
}

TEST_F(HamtTreeTest, DifferentContainerRangesDoNotCompareEqualEvenWhenTheEntireRootIsShared) {
  EXPECT_THAT(tree.try_insert(Entry{.key = 1, .value = 10}), MutationIs(true));
  const Tree copied = tree;
  EXPECT_THAT(tree.begin().operator->(), Eq(copied.begin().operator->()));
  EXPECT_THAT(tree.begin() == copied.begin(), IsFalse());
  EXPECT_THAT(tree.find(1), Eq(tree.begin()));
  EXPECT_THAT(copied.find(1), Eq(copied.begin()));
  EXPECT_THAT(tree.find(1) == copied.find(1), IsFalse());
  auto last = copied.begin();
  EXPECT_THAT(++last, Eq(copied.end()));
}

}  // namespace
}  // namespace mbo::container::container_internal
