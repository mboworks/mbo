// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_shared_node.h"

#include <array>
#include <cstddef>
#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Optional;

struct CountingSource {
  static constexpr bool supports_recoverable_failure = true;

  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    auto block = source.TryAcquire(size, alignment);
    acquired += block.has_value() ? 1 : 0;
    return block;
  }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    ++released;
    source.Release(block);
  }

  mbo::memory::NewDeleteBlockSource source;
  std::size_t acquired = 0;
  std::size_t released = 0;
};

using Node = HamtSharedNode<5, int>;

struct HamtSharedNodeTest : ::testing::Test {};

TEST_F(HamtSharedNodeTest, SharesChildrenAndReclaimsTheTreeAtLastRelease) {
  CountingSource source;
  Node::index_type leaf_index;
  ASSERT_THAT(leaf_index.InsertData(3), Eq(true));
  constexpr auto kLeafEntries = std::to_array<int>({42});
  const auto leaf = Node::TryCreate(source, leaf_index, kLeafEntries, std::span<Node* const>{});
  ASSERT_THAT(leaf, Optional(_));

  Node::index_type parent_index;
  ASSERT_THAT(parent_index.InsertNode(7), Eq(true));
  const auto children = std::to_array<Node*>({*leaf});
  const auto parent = Node::TryCreate(source, parent_index, std::span<const int>{}, children);
  ASSERT_THAT(parent, Optional(_));
  EXPECT_THAT((*leaf)->use_count(), Eq(2));
  EXPECT_THAT((*parent)->children(), ElementsAre(*leaf));
  EXPECT_THAT((*leaf)->entries(), ElementsAre(42));

  Node::Release(source, *leaf);
  EXPECT_THAT((*parent)->children().front()->use_count(), Eq(1));
  Node::Release(source, *parent);
  EXPECT_THAT(source.acquired, Eq(2));
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, RejectsCountsThatDisagreeWithTheIndex) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));

  EXPECT_THAT(Node::TryCreate(source, index, std::span<const int>{}, std::span<Node* const>{}), Eq(std::nullopt));
  EXPECT_THAT(source.acquired, Eq(0));
}

TEST_F(HamtSharedNodeTest, RejectsChildCountsBeforeAllocating) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(1), Eq(true));
  EXPECT_THAT(Node::TryCreate(source, index, {}, {}), Eq(std::nullopt));
  EXPECT_THAT(source.acquired, Eq(0));
}

TEST_F(HamtSharedNodeTest, ExhaustionDoesNotRetainChildren) {
  CountingSource source;
  const auto leaf = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(leaf, Optional(_));
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(1), Eq(true));
  const auto children = std::to_array<Node*>({*leaf});
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryCreate(exhausted, index, {}, children), Eq(std::nullopt));
  EXPECT_THAT((*leaf)->use_count(), Eq(1));
  Node::Release(source, *leaf);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, EmptyNodeCanBeRetainedAndReleasedThroughConstViews) {
  CountingSource source;
  const auto node = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(node, Optional(_));
  const Node& view = **node;
  EXPECT_THAT(view.entries().empty(), Eq(true));
  EXPECT_THAT(view.children().empty(), Eq(true));
  Node::Retain(*node);
  Node::Release(source, *node);
  EXPECT_THAT(view.use_count(), Eq(1));
  EXPECT_THAT(source.released, Eq(0));
  Node::Release(source, *node);
  EXPECT_THAT(source.released, Eq(1));
  Node::Retain(nullptr);
  Node::Release(source, nullptr);
  EXPECT_THAT(source.released, Eq(1));
}

struct OversizedSource final : CountingSource {
  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    block = source.TryAcquire(size + 64, alignment);
    return block;
  }

  void Release(mbo::memory::MemoryBlock released_block) noexcept {
    EXPECT_THAT(released_block.data, Eq(block->data));
    EXPECT_THAT(released_block.size, Eq(block->size));
    EXPECT_THAT(released_block.alignment, Eq(block->alignment));
    CountingSource::Release(released_block);
  }

  std::optional<mbo::memory::MemoryBlock> block;
};

TEST_F(HamtSharedNodeTest, ReleasesTheOriginalAllocationMetadata) {
  OversizedSource source;
  const auto node = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(node, Optional(_));
  Node::Release(source, *node);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, CollisionNodeCanExceedTheIndexedSlotCount) {
  CountingSource source;
  std::array<int, Node::index_type::kSlotCount + 1> entries{};
  for (std::size_t index = 0; index < entries.size(); ++index) {
    entries[index] = static_cast<int>(index);
  }
  const auto node = Node::TryCreateCollision(source, entries);
  ASSERT_THAT(node, Optional(_));
  const Node& view = **node;
  EXPECT_THAT(view.is_collision(), Eq(true));
  EXPECT_THAT(view.entries(), ElementsAreArray(entries));
  EXPECT_THAT(view.children().empty(), Eq(true));
  EXPECT_THAT(view.index().DataSize(), Eq(0));
  Node::Retain(*node);
  Node::Release(source, *node);
  EXPECT_THAT(source.released, Eq(0));
  Node::Release(source, *node);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, EmptyCollisionIsRejectedBeforeAllocation) {
  CountingSource source;
  EXPECT_THAT(Node::TryCreateCollision(source, {}), Eq(std::nullopt));
  EXPECT_THAT(source.acquired, Eq(0));
}

TEST_F(HamtSharedNodeTest, CollisionReleasePreservesOriginalAllocationMetadata) {
  OversizedSource source;
  constexpr auto kEntries = std::to_array<int>({1, 2, 3});
  const auto node = Node::TryCreateCollision(source, kEntries);
  ASSERT_THAT(node, Optional(_));
  EXPECT_THAT((*node)->entries(), ElementsAre(1, 2, 3));
  Node::Release(source, *node);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, CollisionAllocationReportsExhaustion) {
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  constexpr auto kEntries = std::to_array<int>({1, 2});
  EXPECT_THAT(Node::TryCreateCollision(exhausted, kEntries), Eq(std::nullopt));
}

TEST_F(HamtSharedNodeTest, InsertsWithoutChangingTheOriginalNode) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertData(3), Eq(true));
  constexpr auto kEntries = std::to_array<int>({10, 30});
  const auto original = Node::TryCreate(source, index, kEntries, {});
  ASSERT_THAT(original, Optional(_));
  ASSERT_THAT(index.InsertData(2), Eq(true));
  const auto inserted = Node::TryInsertEntry(source, **original, index, 1, 20);
  ASSERT_THAT(inserted, Optional(_));
  EXPECT_THAT((*original)->entries(), ElementsAre(10, 30));
  EXPECT_THAT((*inserted)->entries(), ElementsAre(10, 20, 30));
  Node::Release(source, *original);
  EXPECT_THAT((*inserted)->entries(), ElementsAre(10, 20, 30));
  Node::Release(source, *inserted);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, InsertsAtBothEndsOfTheDenseEntryArray) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertData(2), Eq(true));
  constexpr auto kEntries = std::to_array<int>({20});
  const auto original = Node::TryCreate(source, index, kEntries, {});
  ASSERT_THAT(original, Optional(_));
  auto before_index = index;
  ASSERT_THAT(before_index.InsertData(1), Eq(true));
  const auto before = Node::TryInsertEntry(source, **original, before_index, 0, 10);
  ASSERT_THAT(before, Optional(_));
  auto after_index = index;
  ASSERT_THAT(after_index.InsertData(3), Eq(true));
  const auto after = Node::TryInsertEntry(source, **original, after_index, 1, 30);
  ASSERT_THAT(after, Optional(_));
  EXPECT_THAT((*before)->entries(), ElementsAre(10, 20));
  EXPECT_THAT((*after)->entries(), ElementsAre(20, 30));
  Node::Release(source, *original);
  Node::Release(source, *before);
  Node::Release(source, *after);
  EXPECT_THAT(source.released, Eq(3));
}

TEST_F(HamtSharedNodeTest, InsertionFailurePreservesTheOriginal) {
  CountingSource source;
  const auto original = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(original, Optional(_));
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  EXPECT_THAT(Node::TryInsertEntry(source, **original, {}, 0, 10), Eq(std::nullopt));
  EXPECT_THAT(Node::TryInsertEntry(source, **original, index, 1, 10), Eq(std::nullopt));
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryInsertEntry(exhausted, **original, index, 0, 10), Eq(std::nullopt));
  EXPECT_THAT((*original)->entries().empty(), Eq(true));
  EXPECT_THAT((*original)->use_count(), Eq(1));
  Node::Release(source, *original);
  EXPECT_THAT(source.acquired, Eq(1));
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, InsertedNodeRetainsSharedChildrenUntilItsLastRelease) {
  CountingSource source;
  const auto leaf = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(leaf, Optional(_));
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(7), Eq(true));
  const auto children = std::to_array<Node*>({*leaf});
  const auto original = Node::TryCreate(source, index, {}, children);
  ASSERT_THAT(original, Optional(_));
  ASSERT_THAT(index.InsertData(1), Eq(true));
  const auto inserted = Node::TryInsertEntry(source, **original, index, 0, 10);
  ASSERT_THAT(inserted, Optional(_));
  EXPECT_THAT((*leaf)->use_count(), Eq(3));
  Node::Release(source, *original);
  EXPECT_THAT((*leaf)->use_count(), Eq(2));
  Node::Release(source, *leaf);
  EXPECT_THAT((*inserted)->children().front()->use_count(), Eq(1));
  EXPECT_THAT((*inserted)->entries(), ElementsAre(10));
  Node::Release(source, *inserted);
  EXPECT_THAT(source.released, Eq(3));
}

TEST_F(HamtSharedNodeTest, InsertsChildrenAtBothEndsWithoutChangingOriginal) {
  CountingSource source;
  const auto existing = Node::TryCreate(source, {}, {}, {});
  const auto added = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(existing, Optional(_));
  ASSERT_THAT(added, Optional(_));
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertNode(4), Eq(true));
  constexpr auto kEntries = std::to_array<int>({10});
  const auto children = std::to_array<Node*>({*existing});
  const auto original = Node::TryCreate(source, index, kEntries, children);
  ASSERT_THAT(original, Optional(_));
  auto before_index = index;
  ASSERT_THAT(before_index.InsertNode(2), Eq(true));
  const auto before = Node::TryInsertChild(source, **original, before_index, 0, *added);
  ASSERT_THAT(before, Optional(_));
  auto after_index = index;
  ASSERT_THAT(after_index.InsertNode(6), Eq(true));
  const auto after = Node::TryInsertChild(source, **original, after_index, 1, *added);
  ASSERT_THAT(after, Optional(_));
  EXPECT_THAT((*original)->children(), ElementsAre(*existing));
  EXPECT_THAT((*before)->children(), ElementsAre(*added, *existing));
  EXPECT_THAT((*after)->children(), ElementsAre(*existing, *added));
  EXPECT_THAT((*before)->entries(), ElementsAre(10));
  EXPECT_THAT((*after)->entries(), ElementsAre(10));
  EXPECT_THAT((*existing)->use_count(), Eq(4));
  EXPECT_THAT((*added)->use_count(), Eq(3));
  Node::Release(source, *existing);
  Node::Release(source, *added);
  Node::Release(source, *original);
  Node::Release(source, *before);
  EXPECT_THAT((*after)->children().front()->use_count(), Eq(1));
  Node::Release(source, *after);
  EXPECT_THAT(source.released, Eq(5));
}

TEST_F(HamtSharedNodeTest, FailedChildInsertionDoesNotRetainTheChild) {
  CountingSource source;
  const auto original = Node::TryCreate(source, {}, {}, {});
  const auto child = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(original, Optional(_));
  ASSERT_THAT(child, Optional(_));
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  EXPECT_THAT(Node::TryInsertChild(source, **original, index, 0, nullptr), Eq(std::nullopt));
  EXPECT_THAT(Node::TryInsertChild(source, **original, {}, 0, *child), Eq(std::nullopt));
  EXPECT_THAT(Node::TryInsertChild(source, **original, index, 1, *child), Eq(std::nullopt));
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryInsertChild(exhausted, **original, index, 0, *child), Eq(std::nullopt));
  EXPECT_THAT((*child)->use_count(), Eq(1));
  EXPECT_THAT((*original)->children().empty(), Eq(true));
  EXPECT_THAT(source.acquired, Eq(2));
  Node::Release(source, *original);
  Node::Release(source, *child);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, ReplacementRetainsOnlyTheNewChildForItsCopiedNode) {
  CountingSource source;
  const auto old_child = Node::TryCreate(source, {}, {}, {});
  const auto replacement = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(old_child, Optional(_));
  ASSERT_THAT(replacement, Optional(_));
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  constexpr auto kEntries = std::to_array<int>({10});
  const auto children = std::to_array<Node*>({*old_child});
  const auto original = Node::TryCreate(source, index, kEntries, children);
  ASSERT_THAT(original, Optional(_));
  const auto copied = Node::TryReplaceChild(source, **original, 0, *replacement);
  ASSERT_THAT(copied, Optional(_));
  EXPECT_THAT((*original)->children(), ElementsAre(*old_child));
  EXPECT_THAT((*copied)->children(), ElementsAre(*replacement));
  EXPECT_THAT((*copied)->entries(), ElementsAre(10));
  EXPECT_THAT((*old_child)->use_count(), Eq(2));
  EXPECT_THAT((*replacement)->use_count(), Eq(2));
  Node::Release(source, *old_child);
  Node::Release(source, *replacement);
  Node::Release(source, *original);
  EXPECT_THAT(source.released, Eq(2));
  EXPECT_THAT((*copied)->children().front()->use_count(), Eq(1));
  Node::Release(source, *copied);
  EXPECT_THAT(source.released, Eq(4));
}

TEST_F(HamtSharedNodeTest, SelfReplacementCreatesAnotherOwnerOfTheSameChild) {
  CountingSource source;
  const auto child = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(child, Optional(_));
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  const auto children = std::to_array<Node*>({*child});
  const auto original = Node::TryCreate(source, index, {}, children);
  ASSERT_THAT(original, Optional(_));
  const auto copied = Node::TryReplaceChild(source, **original, 0, *child);
  ASSERT_THAT(copied, Optional(_));
  EXPECT_THAT((*child)->use_count(), Eq(3));
  Node::Release(source, *original);
  Node::Release(source, *child);
  EXPECT_THAT((*copied)->children().front()->use_count(), Eq(1));
  Node::Release(source, *copied);
  EXPECT_THAT(source.released, Eq(3));
}

TEST_F(HamtSharedNodeTest, FailedReplacementLeavesBothChildrenUnchanged) {
  CountingSource source;
  const auto child = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(child, Optional(_));
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  const auto children = std::to_array<Node*>({*child});
  const auto original = Node::TryCreate(source, index, {}, children);
  ASSERT_THAT(original, Optional(_));
  EXPECT_THAT(Node::TryReplaceChild(source, **original, 0, nullptr), Eq(std::nullopt));
  EXPECT_THAT(Node::TryReplaceChild(source, **original, 1, *child), Eq(std::nullopt));
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryReplaceChild(exhausted, **original, 0, *child), Eq(std::nullopt));
  EXPECT_THAT((*child)->use_count(), Eq(2));
  EXPECT_THAT((*original)->children(), ElementsAre(*child));
  Node::Release(source, *original);
  Node::Release(source, *child);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, ErasesEachDenseEntryWithoutChangingOriginal) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertData(2), Eq(true));
  ASSERT_THAT(index.InsertData(3), Eq(true));
  constexpr auto kEntries = std::to_array<int>({10, 20, 30});
  const auto original = Node::TryCreate(source, index, kEntries, {});
  ASSERT_THAT(original, Optional(_));
  for (std::size_t position = 0; position < kEntries.size(); ++position) {
    auto erased_index = index;
    ASSERT_THAT(erased_index.EraseData(position + 1), Eq(true));
    const auto erased = Node::TryEraseEntry(source, **original, erased_index, position);
    ASSERT_THAT(erased, Optional(_));
    EXPECT_THAT((*erased)->entries().size(), Eq(2));
    for (std::size_t remaining = 0; remaining < 2; ++remaining) {
      EXPECT_THAT((*erased)->entries()[remaining], Eq(kEntries[remaining + (remaining >= position ? 1 : 0)]));
    }
    EXPECT_THAT((*original)->entries(), ElementsAre(10, 20, 30));
    Node::Release(source, *erased);
  }
  Node::Release(source, *original);
  EXPECT_THAT(source.released, Eq(4));
}

TEST_F(HamtSharedNodeTest, ErasingTheLastEntryRetainsItsSharedChildren) {
  CountingSource source;
  const auto child = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(child, Optional(_));
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  constexpr auto kEntries = std::to_array<int>({10});
  const auto children = std::to_array<Node*>({*child});
  const auto original = Node::TryCreate(source, index, kEntries, children);
  ASSERT_THAT(original, Optional(_));
  ASSERT_THAT(index.EraseData(1), Eq(true));
  const auto erased = Node::TryEraseEntry(source, **original, index, 0);
  ASSERT_THAT(erased, Optional(_));
  EXPECT_THAT((*erased)->entries().empty(), Eq(true));
  EXPECT_THAT((*child)->use_count(), Eq(3));
  Node::Release(source, *original);
  Node::Release(source, *child);
  EXPECT_THAT((*erased)->children().front()->use_count(), Eq(1));
  Node::Release(source, *erased);
  EXPECT_THAT(source.released, Eq(3));
}

TEST_F(HamtSharedNodeTest, FailedErasurePreservesEntries) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  constexpr auto kEntries = std::to_array<int>({10});
  const auto original = Node::TryCreate(source, index, kEntries, {});
  ASSERT_THAT(original, Optional(_));
  EXPECT_THAT(Node::TryEraseEntry(source, **original, index, 0), Eq(std::nullopt));
  EXPECT_THAT(Node::TryEraseEntry(source, **original, {}, 1), Eq(std::nullopt));
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryEraseEntry(exhausted, **original, {}, 0), Eq(std::nullopt));
  EXPECT_THAT((*original)->entries(), ElementsAre(10));
  EXPECT_THAT(source.acquired, Eq(1));
  Node::Release(source, *original);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, ErasesEachChildWhilePreservingTheOriginalOwners) {
  CountingSource source;
  std::array<Node*, 3> children{};
  Node::index_type index;
  for (std::size_t position = 0; position < children.size(); ++position) {
    const auto child = Node::TryCreate(source, {}, {}, {});
    ASSERT_THAT(child, Optional(_));
    children[position] = *child;
    ASSERT_THAT(index.InsertNode(position + 1), Eq(true));
  }
  ASSERT_THAT(index.InsertData(7), Eq(true));
  constexpr auto kEntries = std::to_array<int>({70});
  const auto original = Node::TryCreate(source, index, kEntries, children);
  ASSERT_THAT(original, Optional(_));
  for (std::size_t position = 0; position < children.size(); ++position) {
    auto erased_index = index;
    ASSERT_THAT(erased_index.EraseNode(position + 1), Eq(true));
    const auto erased = Node::TryEraseChild(source, **original, erased_index, position);
    ASSERT_THAT(erased, Optional(_));
    EXPECT_THAT((*erased)->entries(), ElementsAre(70));
    EXPECT_THAT((*erased)->children().size(), Eq(2));
    EXPECT_THAT(children[position]->use_count(), Eq(2));
    for (std::size_t remaining = 0; remaining < 2; ++remaining) {
      Node* const expected = children[remaining + (remaining >= position ? 1 : 0)];
      EXPECT_THAT((*erased)->children()[remaining], Eq(expected));
      EXPECT_THAT(expected->use_count(), Eq(3));
    }
    Node::Release(source, *erased);
    EXPECT_THAT((*original)->children(), ElementsAreArray(children));
  }
  Node::Release(source, *original);
  for (Node* child : children) {
    EXPECT_THAT(child->use_count(), Eq(1));
    Node::Release(source, child);
  }
  EXPECT_THAT(source.released, Eq(7));
}

TEST_F(HamtSharedNodeTest, ErasesTheOnlyChildIntoAnEmptyNode) {
  CountingSource source;
  const auto child = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(child, Optional(_));
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(1), Eq(true));
  const auto children = std::to_array<Node*>({*child});
  const auto original = Node::TryCreate(source, index, {}, children);
  ASSERT_THAT(original, Optional(_));
  const auto erased = Node::TryEraseChild(source, **original, {}, 0);
  ASSERT_THAT(erased, Optional(_));
  EXPECT_THAT((*erased)->children().empty(), Eq(true));
  EXPECT_THAT((*erased)->entries().empty(), Eq(true));
  EXPECT_THAT((*child)->use_count(), Eq(2));
  Node::Release(source, *original);
  Node::Release(source, *child);
  Node::Release(source, *erased);
  EXPECT_THAT(source.released, Eq(3));
}

TEST_F(HamtSharedNodeTest, FailedChildErasurePreservesReferenceCounts) {
  CountingSource source;
  const auto child = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(child, Optional(_));
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(1), Eq(true));
  const auto children = std::to_array<Node*>({*child});
  const auto original = Node::TryCreate(source, index, {}, children);
  ASSERT_THAT(original, Optional(_));
  EXPECT_THAT(Node::TryEraseChild(source, **original, index, 0), Eq(std::nullopt));
  EXPECT_THAT(Node::TryEraseChild(source, **original, {}, 1), Eq(std::nullopt));
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryEraseChild(exhausted, **original, {}, 0), Eq(std::nullopt));
  EXPECT_THAT((*child)->use_count(), Eq(2));
  EXPECT_THAT((*original)->children(), ElementsAre(*child));
  Node::Release(source, *original);
  Node::Release(source, *child);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, CollisionInsertionPreservesOrderAtEveryPosition) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({10, 30});
  const auto original = Node::TryCreateCollision(source, kEntries);
  ASSERT_THAT(original, Optional(_));
  for (std::size_t position = 0; position <= kEntries.size(); ++position) {
    SCOPED_TRACE(position);
    const auto inserted = Node::TryInsertCollisionEntry(source, **original, position, 20);
    ASSERT_THAT(inserted, Optional(_));
    EXPECT_THAT((*inserted)->is_collision(), Eq(true));
    EXPECT_THAT((*inserted)->children().empty(), Eq(true));
    ASSERT_THAT((*inserted)->entries().size(), Eq(3));
    EXPECT_THAT((*inserted)->entries()[position], Eq(20));
    for (std::size_t existing = 0; existing < kEntries.size(); ++existing) {
      EXPECT_THAT((*inserted)->entries()[existing + (existing >= position ? 1 : 0)], Eq(kEntries[existing]));
    }
    EXPECT_THAT((*original)->entries(), ElementsAre(10, 30));
    Node::Release(source, *inserted);
  }
  Node::Release(source, *original);
  EXPECT_THAT(source.released, Eq(4));
}

TEST_F(HamtSharedNodeTest, CollisionErasureCanLeaveASingleEntry) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({10, 20});
  const auto original = Node::TryCreateCollision(source, kEntries);
  ASSERT_THAT(original, Optional(_));
  for (std::size_t position = 0; position < kEntries.size(); ++position) {
    SCOPED_TRACE(position);
    const auto erased = Node::TryEraseCollisionEntry(source, **original, position);
    ASSERT_THAT(erased, Optional(_));
    EXPECT_THAT((*erased)->is_collision(), Eq(true));
    EXPECT_THAT((*erased)->entries(), ElementsAre(kEntries[1 - position]));
    EXPECT_THAT(Node::TryEraseCollisionEntry(source, **erased, 0), Eq(std::nullopt));
    EXPECT_THAT((*original)->entries(), ElementsAre(10, 20));
    Node::Release(source, *erased);
  }
  Node::Release(source, *original);
  EXPECT_THAT(source.released, Eq(3));
}

TEST_F(HamtSharedNodeTest, CollisionMutationFailureDoesNotChangeOriginal) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({10, 20});
  const auto original = Node::TryCreateCollision(source, kEntries);
  ASSERT_THAT(original, Optional(_));
  EXPECT_THAT(Node::TryInsertCollisionEntry(source, **original, 3, 30), Eq(std::nullopt));
  EXPECT_THAT(Node::TryEraseCollisionEntry(source, **original, 2), Eq(std::nullopt));
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryInsertCollisionEntry(exhausted, **original, 2, 30), Eq(std::nullopt));
  EXPECT_THAT(Node::TryEraseCollisionEntry(exhausted, **original, 0), Eq(std::nullopt));
  EXPECT_THAT((*original)->entries(), ElementsAre(10, 20));
  EXPECT_THAT((*original)->use_count(), Eq(1));
  EXPECT_THAT(source.acquired, Eq(1));
  Node::Release(source, *original);
  EXPECT_THAT(source.released, Eq(1));
}

}  // namespace
}  // namespace mbo::container::container_internal
