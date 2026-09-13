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

TEST_F(HamtSharedNodeTest, InsertionCanBorrowAnEntryFromTheOriginalNode) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertData(3), Eq(true));
  ASSERT_THAT(index.InsertData(7), Eq(true));
  constexpr auto kEntries = std::to_array<int>({3, 5});
  const auto original = Node::TryCreate(source, index, kEntries, {});
  ASSERT_THAT(original, Optional(_));
  ASSERT_THAT(index.InsertData(5), Eq(true));
  const auto inserted = Node::TryInsertEntry(source, **original, index, 1, (*original)->entries()[0]);
  ASSERT_THAT(inserted, Optional(_));
  EXPECT_THAT((*original)->entries(), ElementsAre(3, 5));
  Node::Release(source, *original);
  EXPECT_THAT((*inserted)->entries(), ElementsAre(3, 3, 5));
  Node::Release(source, *inserted);
  EXPECT_THAT(source.acquired, Eq(source.released));
}

struct OfferedSource final {
  static constexpr bool supports_recoverable_failure = true;

  static constexpr std::size_t max_alignment() noexcept { return alignof(std::max_align_t); }

  mbo::memory::MemoryBlock offered;
  mbo::memory::MemoryBlock released;
  std::size_t release_count = 0;

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t, std::size_t) noexcept { return offered; }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    released = block;
    ++release_count;
  }
};

TEST_F(HamtSharedNodeTest, RejectsUnusableBlocksWithoutRetainingChildren) {
  CountingSource source;
  const auto leaf = Node::TryCreate(source, {}, {}, {});
  ASSERT_THAT(leaf, Optional(_));
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(3), Eq(true));
  const auto children = std::to_array<Node*>({*leaf});
  alignas(std::max_align_t) std::array<std::byte, 256> storage{};
  const auto offers = std::to_array<mbo::memory::MemoryBlock>({
      {.data = nullptr, .size = storage.size(), .alignment = alignof(std::max_align_t)},
      {.data = storage.data(), .size = 1, .alignment = alignof(std::max_align_t)},
      {.data = storage.data(), .size = storage.size(), .alignment = 1},
      {.data = storage.data() + 1, .size = storage.size() - 1, .alignment = alignof(std::max_align_t)},
  });
  for (const auto offer : offers) {
    OfferedSource invalid{.offered = offer, .released = {}};
    EXPECT_THAT(Node::TryCreate(invalid, index, {}, children), Eq(std::nullopt));
    EXPECT_THAT((*leaf)->use_count(), Eq(1));
    EXPECT_THAT(invalid.release_count, Eq(1));
    EXPECT_THAT(invalid.released, Eq(offer));
  }
  Node::Release(source, *leaf);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, RejectsOccupiedSlotsWithoutAChildBeforeAllocation) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(7), Eq(true));
  const auto children = std::to_array<Node*>({nullptr});
  EXPECT_THAT(Node::TryCreate(source, index, {}, children).has_value(), Eq(false));
  EXPECT_THAT(source.acquired, Eq(std::size_t{0}));
  EXPECT_THAT(source.released, Eq(std::size_t{0}));
}

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

TEST_F(HamtSharedNodeTest, SingletonCollisionKeepsItsRepresentationAcrossSharedOwnership) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({42});
  const auto node = Node::TryCreateCollision(source, kEntries);
  ASSERT_THAT(node, Optional(_));
  Node::Retain(*node);
  Node::Release(source, *node);
  const Node& view = **node;
  EXPECT_THAT(view.is_collision(), Eq(true));
  EXPECT_THAT(view.entries(), ElementsAre(42));
  EXPECT_THAT(view.children().empty(), Eq(true));
  EXPECT_THAT(view.index().DataSize(), Eq(0));
  EXPECT_THAT(view.index().NodeSize(), Eq(0));
  EXPECT_THAT(view.use_count(), Eq(1));
  EXPECT_THAT(source.released, Eq(0));
  Node::Release(source, *node);
  EXPECT_THAT(source.released, Eq(1));
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

}  // namespace
}  // namespace mbo::container::container_internal
