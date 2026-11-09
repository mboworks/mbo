// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/experimental/hamt/internal/hamt_shared_node.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::experimental::hamt::hamt_internal {
namespace {

using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Ne;
using ::testing::NotNull;

struct LifetimeCounts final {
  std::array<int, 4> live{};
  int constructed = 0;
  int destroyed = 0;
};

struct TrackedEntry final {
  TrackedEntry(LifetimeCounts& counts, int value) noexcept : counts(counts), value(value) {
    ++counts.live.at(value);
    ++counts.constructed;
  }

  TrackedEntry(const TrackedEntry& other) noexcept : TrackedEntry(other.counts, other.value) {}

  TrackedEntry& operator=(const TrackedEntry&) = delete;

  TrackedEntry(TrackedEntry&& other) noexcept : TrackedEntry(other.counts, other.value) {}

  TrackedEntry& operator=(TrackedEntry&&) = delete;

  ~TrackedEntry() {
    --counts.live.at(value);
    ++counts.destroyed;
  }

  LifetimeCounts& counts;
  int value;
};

struct CountingSource final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    const auto block = mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
    acquired += block.has_value() ? 1 : 0;
    return block;
  }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    ++released;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }

  int acquired = 0;
  int released = 0;
};

struct HamtSharedNodeLifetimeTest : ::testing::Test {
  using Node = HamtSharedNode<5, TrackedEntry>;

 protected:
  void TearDown() override {
    for (Node* owner : owners) {
      Node::Release(source, owner);
    }
    EXPECT_THAT(counts.live, Each(0));
    EXPECT_THAT(counts.constructed, Eq(counts.destroyed));
    EXPECT_THAT(source.acquired, Eq(source.released));
  }

 public:
  Node* Own(std::optional<Node*> node) {
    if (node) {
      owners.push_back(*node);
    }
    return node.value_or(nullptr);
  }

  void Share(Node* node) {
    Node::Retain(node);
    owners.push_back(node);
  }

  void Drop(Node* node) {
    const auto found = std::ranges::find(owners, node);
    ASSERT_THAT(found, Ne(owners.end()));
    owners.erase(found);
    Node::Release(source, node);
  }

  void CheckReplacement(bool collision) {
    const auto entries = std::to_array<TrackedEntry>({{counts, 0}, {counts, 1}, {counts, 2}});
    const TrackedEntry replacement(counts, 3);
    Node::index_type index;
    ASSERT_THAT(index.InsertData(1), Eq(true));
    ASSERT_THAT(index.InsertData(3), Eq(true));
    ASSERT_THAT(index.InsertData(5), Eq(true));
    auto* const original =
        Own(collision ? Node::TryCreateCollision(source, entries) : Node::TryCreate(source, index, entries, {}));
    ASSERT_THAT(original, NotNull());
    Share(original);
    auto* const replaced = Own(Node::TryReplaceEntry(source, *original, 1, replacement));
    ASSERT_THAT(replaced, NotNull());
    EXPECT_THAT(counts.live, ElementsAre(3, 2, 3, 2));
    Drop(original);
    EXPECT_THAT(counts.live, ElementsAre(3, 2, 3, 2));
    Drop(original);
    EXPECT_THAT(counts.live, ElementsAre(2, 1, 2, 2));
    EXPECT_THAT(
        replaced->entries(), ElementsAre(
                                 Field("value", &TrackedEntry::value, 0), Field("value", &TrackedEntry::value, 3),
                                 Field("value", &TrackedEntry::value, 2)));
    Drop(replaced);
    EXPECT_THAT(counts.live, ElementsAre(1, 1, 1, 1));
  }

  LifetimeCounts counts;
  CountingSource source;
  std::vector<Node*> owners;
};

TEST_F(HamtSharedNodeLifetimeTest, EraseEntryPreservesTheSharedOriginalUntilItsLastOwnerReleases) {
  const auto entries = std::to_array<TrackedEntry>({{counts, 0}, {counts, 1}, {counts, 2}});
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertData(3), Eq(true));
  ASSERT_THAT(index.InsertData(5), Eq(true));
  auto* const original = Own(Node::TryCreate(source, index, entries, {}));
  ASSERT_THAT(original, NotNull());
  Share(original);
  ASSERT_THAT(index.EraseData(3), Eq(true));
  auto* const erased = Own(Node::TryEraseEntry(source, *original, index, 1));
  ASSERT_THAT(erased, NotNull());
  EXPECT_THAT(counts.live, ElementsAre(3, 2, 3, 0));
  Drop(original);
  EXPECT_THAT(counts.live, ElementsAre(3, 2, 3, 0));
  Drop(original);
  EXPECT_THAT(counts.live, ElementsAre(2, 1, 2, 0));
  EXPECT_THAT(
      erased->entries(), ElementsAre(Field("value", &TrackedEntry::value, 0), Field("value", &TrackedEntry::value, 2)));
  Drop(erased);
  EXPECT_THAT(counts.live, ElementsAre(1, 1, 1, 0));
}

TEST_F(HamtSharedNodeLifetimeTest, EraseChildReleasesOnlyTheRemovedSubtreeAfterTheLastSnapshot) {
  const auto left_entries = std::to_array<TrackedEntry>({{counts, 0}});
  const auto right_entries = std::to_array<TrackedEntry>({{counts, 1}});
  const auto parent_entries = std::to_array<TrackedEntry>({{counts, 2}});
  Node::index_type leaf_index;
  ASSERT_THAT(leaf_index.InsertData(0), Eq(true));
  auto* const left = Own(Node::TryCreate(source, leaf_index, left_entries, {}));
  auto* const right = Own(Node::TryCreate(source, leaf_index, right_entries, {}));
  ASSERT_THAT(left, NotNull());
  ASSERT_THAT(right, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(1), Eq(true));
  ASSERT_THAT(index.InsertNode(3), Eq(true));
  ASSERT_THAT(index.InsertData(5), Eq(true));
  const auto children = std::to_array<Node*>({left, right});
  auto* const original = Own(Node::TryCreate(source, index, parent_entries, children));
  ASSERT_THAT(original, NotNull());
  Share(original);
  ASSERT_THAT(index.EraseNode(1), Eq(true));
  auto* const erased = Own(Node::TryEraseChild(source, *original, index, 0));
  ASSERT_THAT(erased, NotNull());
  EXPECT_THAT(counts.live, ElementsAre(2, 2, 3, 0));
  Drop(left);
  Drop(right);
  Drop(original);
  EXPECT_THAT(counts.live, ElementsAre(2, 2, 3, 0));
  Drop(original);
  EXPECT_THAT(counts.live, ElementsAre(1, 2, 2, 0));
  EXPECT_THAT(erased->children(), ElementsAre(right));
  Drop(erased);
  EXPECT_THAT(counts.live, ElementsAre(1, 1, 1, 0));
}

TEST_F(HamtSharedNodeLifetimeTest, DemotionCopiesThePayloadBeforeTheSharedChildsLastRelease) {
  const auto parent_entries = std::to_array<TrackedEntry>({{counts, 0}});
  const auto child_entries = std::to_array<TrackedEntry>({{counts, 1}});
  Node::index_type leaf_index;
  ASSERT_THAT(leaf_index.InsertData(0), Eq(true));
  auto* const child = Own(Node::TryCreate(source, leaf_index, child_entries, {}));
  ASSERT_THAT(child, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertNode(3), Eq(true));
  const auto children = std::to_array<Node*>({child});
  auto* const original = Own(Node::TryCreate(source, index, parent_entries, children));
  ASSERT_THAT(original, NotNull());
  Share(original);
  ASSERT_THAT(index.EraseNode(3), Eq(true));
  ASSERT_THAT(index.InsertData(3), Eq(true));
  auto* const demoted = Own(Node::TryDemoteChildToEntry(source, *original, index, 0, 1, child->entries().front()));
  ASSERT_THAT(demoted, NotNull());
  EXPECT_THAT(counts.live, ElementsAre(3, 3, 0, 0));
  Drop(child);
  Drop(original);
  EXPECT_THAT(counts.live, ElementsAre(3, 3, 0, 0));
  Drop(original);
  EXPECT_THAT(counts.live, ElementsAre(2, 2, 0, 0));
  EXPECT_THAT(
      demoted->entries(),
      ElementsAre(Field("value", &TrackedEntry::value, 0), Field("value", &TrackedEntry::value, 1)));
  Drop(demoted);
  EXPECT_THAT(counts.live, ElementsAre(1, 1, 0, 0));
}

TEST_F(HamtSharedNodeLifetimeTest, EraseCollisionEntryPreservesSharedPayloadsAndDestroysEachCopyOnce) {
  const auto entries = std::to_array<TrackedEntry>({{counts, 0}, {counts, 1}, {counts, 2}});
  auto* const original = Own(Node::TryCreateCollision(source, entries));
  ASSERT_THAT(original, NotNull());
  Share(original);
  auto* const erased = Own(Node::TryEraseCollisionEntry(source, *original, 1));
  ASSERT_THAT(erased, NotNull());
  EXPECT_THAT(counts.live, ElementsAre(3, 2, 3, 0));
  Drop(original);
  EXPECT_THAT(counts.live, ElementsAre(3, 2, 3, 0));
  Drop(original);
  EXPECT_THAT(counts.live, ElementsAre(2, 1, 2, 0));
  Drop(erased);
  EXPECT_THAT(counts.live, ElementsAre(1, 1, 1, 0));
}

TEST_F(HamtSharedNodeLifetimeTest, ReplaceRegularEntryDestroysTheOldAndNewCopiesAtTheirOwnersLastRelease) {
  CheckReplacement(false);
}

TEST_F(HamtSharedNodeLifetimeTest, ReplaceCollisionEntryDestroysTheOldAndNewCopiesAtTheirOwnersLastRelease) {
  CheckReplacement(true);
}

TEST_F(HamtSharedNodeLifetimeTest, AllocationFailureDoesNotCopyDestroyOrRetainPublishedPayloads) {
  const auto entries = std::to_array<TrackedEntry>({{counts, 0}, {counts, 1}});
  const auto child_entries = std::to_array<TrackedEntry>({{counts, 2}});
  const TrackedEntry replacement(counts, 3);
  Node::index_type leaf_index;
  ASSERT_THAT(leaf_index.InsertData(0), Eq(true));
  auto* const child = Own(Node::TryCreate(source, leaf_index, child_entries, {}));
  ASSERT_THAT(child, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertData(3), Eq(true));
  ASSERT_THAT(index.InsertNode(7), Eq(true));
  const auto children = std::to_array<Node*>({child});
  auto* const original = Own(Node::TryCreate(source, index, entries, children));
  auto* const collision = Own(Node::TryCreateCollision(source, entries));
  ASSERT_THAT(original, NotNull());
  ASSERT_THAT(collision, NotNull());
  Share(original);
  const auto before = counts.live;
  const int constructed = counts.constructed;
  const int destroyed = counts.destroyed;
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  auto erased_entry = index;
  ASSERT_THAT(erased_entry.EraseData(3), Eq(true));
  EXPECT_THAT(Node::TryEraseEntry(exhausted, *original, erased_entry, 1), Eq(std::nullopt));
  auto erased_child = index;
  ASSERT_THAT(erased_child.EraseNode(7), Eq(true));
  EXPECT_THAT(Node::TryEraseChild(exhausted, *original, erased_child, 0), Eq(std::nullopt));
  EXPECT_THAT(Node::TryEraseCollisionEntry(exhausted, *collision, 0), Eq(std::nullopt));
  EXPECT_THAT(Node::TryReplaceEntry(exhausted, *original, 0, replacement), Eq(std::nullopt));
  ASSERT_THAT(erased_child.InsertData(7), Eq(true));
  EXPECT_THAT(
      Node::TryDemoteChildToEntry(exhausted, *original, erased_child, 0, 2, child->entries().front()),
      Eq(std::nullopt));
  EXPECT_THAT(counts.live, Eq(before));
  EXPECT_THAT(counts.constructed, Eq(constructed));
  EXPECT_THAT(counts.destroyed, Eq(destroyed));
  EXPECT_THAT(original->use_count(), Eq(2));
  EXPECT_THAT(child->use_count(), Eq(2));
}

}  // namespace
}  // namespace mbo::container::experimental::hamt::hamt_internal
