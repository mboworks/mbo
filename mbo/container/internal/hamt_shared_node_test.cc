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

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::SizeIs;

struct CountingSource {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    auto block = mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
    acquired += block.has_value() ? 1 : 0;
    return block;
  }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    ++released;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }

  std::size_t acquired = 0;
  std::size_t released = 0;
};

using Node = HamtSharedNode<5, int>;

struct HamtSharedNodeTest : ::testing::Test {};

TEST_F(HamtSharedNodeTest, DemotionRejectsInvalidOccupancyAndPositionsBeforeAllocation) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({42});
  auto* const empty = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  auto* const collision = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(empty, NotNull());
  ASSERT_THAT(collision, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(1), Eq(true));
  const auto children = std::to_array<Node*>({empty});
  auto* const original = Node::TryCreate(source, index, {}, children).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  Node::index_type demoted;
  ASSERT_THAT(demoted.InsertData(1), Eq(true));
  const std::size_t acquired = source.acquired;
  EXPECT_THAT(Node::TryDemoteChildToEntry(source, *collision, demoted, 0, 0, 42), Eq(std::nullopt));
  EXPECT_THAT(Node::TryDemoteChildToEntry(source, *empty, demoted, 0, 0, 42), Eq(std::nullopt));
  EXPECT_THAT(Node::TryDemoteChildToEntry(source, *original, {}, 0, 0, 42), Eq(std::nullopt));
  Node::index_type wrong_nodes = demoted;
  ASSERT_THAT(wrong_nodes.InsertNode(2), Eq(true));
  EXPECT_THAT(Node::TryDemoteChildToEntry(source, *original, wrong_nodes, 0, 0, 42), Eq(std::nullopt));
  EXPECT_THAT(Node::TryDemoteChildToEntry(source, *original, demoted, 1, 0, 42), Eq(std::nullopt));
  EXPECT_THAT(Node::TryDemoteChildToEntry(source, *original, demoted, 0, 1, 42), Eq(std::nullopt));
  EXPECT_THAT(source.acquired, Eq(acquired));
  EXPECT_THAT(original->use_count(), Eq(1));
  EXPECT_THAT(empty->use_count(), Eq(2));
  Node::Release(source, original);
  Node::Release(source, collision);
  Node::Release(source, empty);
  EXPECT_THAT(source.released, Eq(source.acquired));
}

TEST_F(HamtSharedNodeTest, PromotionRejectsInvalidOccupancyAndPositionsBeforeAllocation) {
  CountingSource source;
  auto* const empty = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(empty, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  constexpr auto kEntries = std::to_array<int>({42});
  auto* const original = Node::TryCreate(source, index, kEntries, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  auto* const collision = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(collision, NotNull());
  Node::index_type promoted;
  ASSERT_THAT(promoted.InsertNode(1), Eq(true));
  EXPECT_THAT(Node::TryPromoteEntryToChild(source, *original, promoted, 0, 0, nullptr), Eq(std::nullopt));
  EXPECT_THAT(Node::TryPromoteEntryToChild(source, *collision, promoted, 0, 0, empty), Eq(std::nullopt));
  EXPECT_THAT(Node::TryPromoteEntryToChild(source, *empty, promoted, 0, 0, empty), Eq(std::nullopt));
  EXPECT_THAT(Node::TryPromoteEntryToChild(source, *original, index, 0, 0, empty), Eq(std::nullopt));
  EXPECT_THAT(Node::TryPromoteEntryToChild(source, *original, {}, 0, 0, empty), Eq(std::nullopt));
  EXPECT_THAT(Node::TryPromoteEntryToChild(source, *original, promoted, 1, 0, empty), Eq(std::nullopt));
  EXPECT_THAT(Node::TryPromoteEntryToChild(source, *original, promoted, 0, 1, empty), Eq(std::nullopt));
  EXPECT_THAT(source.acquired, Eq(3));
  EXPECT_THAT(empty->use_count(), Eq(1));
  Node::Release(source, original);
  Node::Release(source, collision);
  Node::Release(source, empty);
  EXPECT_THAT(source.released, Eq(source.acquired));
}

TEST_F(HamtSharedNodeTest, InsertionCanBorrowAnEntryFromTheOriginalNode) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertData(3), Eq(true));
  ASSERT_THAT(index.InsertData(7), Eq(true));
  constexpr auto kEntries = std::to_array<int>({3, 5});
  auto* const original = Node::TryCreate(source, index, kEntries, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  ASSERT_THAT(index.InsertData(5), Eq(true));
  auto* const inserted =
      Node::TryInsertEntry(source, *original, index, 1, original->entries().front()).value_or(nullptr);
  ASSERT_THAT(inserted, NotNull());
  EXPECT_THAT(original->entries(), ElementsAre(3, 5));
  Node::Release(source, original);
  EXPECT_THAT(inserted->entries(), ElementsAre(3, 3, 5));
  Node::Release(source, inserted);
  EXPECT_THAT(source.acquired, Eq(source.released));
}

TEST_F(HamtSharedNodeTest, PadsByteEntriesBeforeChildPointers) {
  using ByteNode = HamtSharedNode<5, std::byte>;
  CountingSource source;
  auto* const leaf = ByteNode::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(leaf, NotNull());
  ByteNode::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  constexpr auto kEntries = std::to_array<std::byte>({std::byte{42}});
  const auto children = std::to_array<ByteNode*>({leaf});
  auto* const parent = ByteNode::TryCreate(source, index, kEntries, children).value_or(nullptr);
  ASSERT_THAT(parent, NotNull());
  EXPECT_THAT(parent->entries(), ElementsAre(std::byte{42}));
  EXPECT_THAT(parent->children(), ElementsAre(leaf));
  ByteNode::Release(source, leaf);
  ByteNode::Release(source, parent);
  EXPECT_THAT(source.released, Eq(2));
}

struct OfferedSource final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract
  static constexpr std::size_t max_alignment() noexcept { return alignof(std::max_align_t); }

  mbo::memory::MemoryBlock offered;
  mbo::memory::MemoryBlock released;
  std::size_t release_count = 0;

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t /*size*/, std::size_t /*alignment*/) const noexcept {
    return offered;
  }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    released = block;
    ++release_count;
  }
};

TEST_F(HamtSharedNodeTest, RejectsUnusableBlocksWithoutRetainingChildren) {
  CountingSource source;
  auto* const leaf = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(leaf, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(3), Eq(true));
  const auto children = std::to_array<Node*>({leaf});
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
    EXPECT_THAT(leaf->use_count(), Eq(1));
    EXPECT_THAT(invalid.release_count, Eq(1));
    EXPECT_THAT(invalid.released, Eq(offer));
  }
  Node::Release(source, leaf);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, RejectsOccupiedSlotsWithoutAChildBeforeAllocation) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(7), Eq(true));
  const auto children = std::to_array<Node*>({nullptr});
  EXPECT_THAT(Node::TryCreate(source, index, {}, children), Eq(std::nullopt));
  EXPECT_THAT(source.acquired, Eq(std::size_t{0}));
  EXPECT_THAT(source.released, Eq(std::size_t{0}));
}

TEST_F(HamtSharedNodeTest, SharesChildrenAndReclaimsTheTreeAtLastRelease) {
  CountingSource source;
  Node::index_type leaf_index;
  ASSERT_THAT(leaf_index.InsertData(3), Eq(true));
  constexpr auto kLeafEntries = std::to_array<int>({42});
  auto* const leaf = Node::TryCreate(source, leaf_index, kLeafEntries, std::span<Node* const>{}).value_or(nullptr);
  ASSERT_THAT(leaf, NotNull());

  Node::index_type parent_index;
  ASSERT_THAT(parent_index.InsertNode(7), Eq(true));
  const auto children = std::to_array<Node*>({leaf});
  auto* const parent = Node::TryCreate(source, parent_index, std::span<const int>{}, children).value_or(nullptr);
  ASSERT_THAT(parent, NotNull());
  EXPECT_THAT(leaf->use_count(), Eq(2));
  EXPECT_THAT(parent->children(), ElementsAre(leaf));
  EXPECT_THAT(leaf->entries(), ElementsAre(42));

  Node::Release(source, leaf);
  EXPECT_THAT(parent->children().front()->use_count(), Eq(1));
  Node::Release(source, parent);
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
  auto* const leaf = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(leaf, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(1), Eq(true));
  const auto children = std::to_array<Node*>({leaf});
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryCreate(exhausted, index, {}, children), Eq(std::nullopt));
  EXPECT_THAT(leaf->use_count(), Eq(1));
  Node::Release(source, leaf);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, EmptyNodeCanBeRetainedAndReleasedThroughConstViews) {
  CountingSource source;
  auto* const node = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(node, NotNull());
  const Node& view = *node;
  EXPECT_THAT(view.entries(), IsEmpty());
  EXPECT_THAT(view.children(), IsEmpty());
  Node::Retain(node);
  Node::Release(source, node);
  EXPECT_THAT(view.use_count(), Eq(1));
  EXPECT_THAT(source.released, Eq(0));
  Node::Release(source, node);
  EXPECT_THAT(source.released, Eq(1));
  Node::Retain(nullptr);
  Node::Release(source, nullptr);
  EXPECT_THAT(source.released, Eq(1));
}

struct OversizedSource final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    block = mbo::memory::NewDeleteBlockSource::TryAcquire(size + 64, alignment);
    return block;
  }

  void Release(mbo::memory::MemoryBlock released_block) noexcept {
    EXPECT_THAT(block, Optional(Eq(released_block)));
    ++released;
    mbo::memory::NewDeleteBlockSource::Release(released_block);
  }

  std::optional<mbo::memory::MemoryBlock> block;
  std::size_t released = 0;
};

TEST_F(HamtSharedNodeTest, ReleasesTheOriginalAllocationMetadata) {
  OversizedSource source;
  auto* const node = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(node, NotNull());
  Node::Release(source, node);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, CollisionNodeCanExceedTheIndexedSlotCount) {
  CountingSource source;
  std::array<int, Node::index_type::kSlotCount + 1> entries{};
  int next_entry = 0;
  for (int& entry : entries) {
    entry = next_entry++;
  }
  auto* const node = Node::TryCreateCollision(source, entries).value_or(nullptr);
  ASSERT_THAT(node, NotNull());
  const Node& view = *node;
  EXPECT_THAT(view.is_collision(), Eq(true));
  EXPECT_THAT(view.entries(), ElementsAreArray(entries));
  EXPECT_THAT(view.children(), IsEmpty());
  EXPECT_THAT(view.index().DataSize(), Eq(0));
  Node::Retain(node);
  Node::Release(source, node);
  EXPECT_THAT(source.released, Eq(0));
  Node::Release(source, node);
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
  auto* const node = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(node, NotNull());
  Node::Retain(node);
  Node::Release(source, node);
  const Node& view = *node;
  EXPECT_THAT(view.is_collision(), Eq(true));
  EXPECT_THAT(view.entries(), ElementsAre(42));
  EXPECT_THAT(view.children(), IsEmpty());
  EXPECT_THAT(view.index().DataSize(), Eq(0));
  EXPECT_THAT(view.index().NodeSize(), Eq(0));
  EXPECT_THAT(view.use_count(), Eq(1));
  EXPECT_THAT(source.released, Eq(0));
  Node::Release(source, node);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, CollisionReleasePreservesOriginalAllocationMetadata) {
  OversizedSource source;
  constexpr auto kEntries = std::to_array<int>({1, 2, 3});
  auto* const node = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(node, NotNull());
  EXPECT_THAT(node->entries(), ElementsAre(1, 2, 3));
  Node::Release(source, node);
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
  auto* const original = Node::TryCreate(source, index, kEntries, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  ASSERT_THAT(index.InsertData(2), Eq(true));
  auto* const inserted = Node::TryInsertEntry(source, *original, index, 1, 20).value_or(nullptr);
  ASSERT_THAT(inserted, NotNull());
  EXPECT_THAT(original->entries(), ElementsAre(10, 30));
  EXPECT_THAT(inserted->entries(), ElementsAre(10, 20, 30));
  Node::Release(source, original);
  EXPECT_THAT(inserted->entries(), ElementsAre(10, 20, 30));
  Node::Release(source, inserted);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, InsertsAtBothEndsOfTheDenseEntryArray) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertData(2), Eq(true));
  constexpr auto kEntries = std::to_array<int>({20});
  auto* const original = Node::TryCreate(source, index, kEntries, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  auto before_index = index;
  ASSERT_THAT(before_index.InsertData(1), Eq(true));
  auto* const before = Node::TryInsertEntry(source, *original, before_index, 0, 10).value_or(nullptr);
  ASSERT_THAT(before, NotNull());
  auto after_index = index;
  ASSERT_THAT(after_index.InsertData(3), Eq(true));
  auto* const after = Node::TryInsertEntry(source, *original, after_index, 1, 30).value_or(nullptr);
  ASSERT_THAT(after, NotNull());
  EXPECT_THAT(before->entries(), ElementsAre(10, 20));
  EXPECT_THAT(after->entries(), ElementsAre(20, 30));
  Node::Release(source, original);
  Node::Release(source, before);
  Node::Release(source, after);
  EXPECT_THAT(source.released, Eq(3));
}

TEST_F(HamtSharedNodeTest, InsertionFailurePreservesTheOriginal) {
  CountingSource source;
  auto* const original = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  EXPECT_THAT(Node::TryInsertEntry(source, *original, {}, 0, 10), Eq(std::nullopt));
  EXPECT_THAT(Node::TryInsertEntry(source, *original, index, 1, 10), Eq(std::nullopt));
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryInsertEntry(exhausted, *original, index, 0, 10), Eq(std::nullopt));
  EXPECT_THAT(original->entries().empty(), Eq(true));
  EXPECT_THAT(original->use_count(), Eq(1));
  Node::Release(source, original);
  EXPECT_THAT(source.acquired, Eq(1));
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, InsertedNodeRetainsSharedChildrenUntilItsLastRelease) {
  CountingSource source;
  auto* const leaf = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(leaf, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(7), Eq(true));
  const auto children = std::to_array<Node*>({leaf});
  auto* const original = Node::TryCreate(source, index, {}, children).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  ASSERT_THAT(index.InsertData(1), Eq(true));
  auto* const inserted = Node::TryInsertEntry(source, *original, index, 0, 10).value_or(nullptr);
  ASSERT_THAT(inserted, NotNull());
  EXPECT_THAT(leaf->use_count(), Eq(3));
  Node::Release(source, original);
  EXPECT_THAT(leaf->use_count(), Eq(2));
  Node::Release(source, leaf);
  EXPECT_THAT(inserted->children().front()->use_count(), Eq(1));
  EXPECT_THAT(inserted->entries(), ElementsAre(10));
  Node::Release(source, inserted);
  EXPECT_THAT(source.released, Eq(3));
}

TEST_F(HamtSharedNodeTest, InsertsChildrenAtBothEndsWithoutChangingOriginal) {
  CountingSource source;
  auto* const existing = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  auto* const added = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(existing, NotNull());
  ASSERT_THAT(added, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertNode(4), Eq(true));
  constexpr auto kEntries = std::to_array<int>({10});
  const auto children = std::to_array<Node*>({existing});
  auto* const original = Node::TryCreate(source, index, kEntries, children).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  auto before_index = index;
  ASSERT_THAT(before_index.InsertNode(2), Eq(true));
  auto* const before = Node::TryInsertChild(source, *original, before_index, 0, added).value_or(nullptr);
  ASSERT_THAT(before, NotNull());
  auto after_index = index;
  ASSERT_THAT(after_index.InsertNode(6), Eq(true));
  auto* const after = Node::TryInsertChild(source, *original, after_index, 1, added).value_or(nullptr);
  ASSERT_THAT(after, NotNull());
  EXPECT_THAT(original->children(), ElementsAre(existing));
  EXPECT_THAT(before->children(), ElementsAre(added, existing));
  EXPECT_THAT(after->children(), ElementsAre(existing, added));
  EXPECT_THAT(before->entries(), ElementsAre(10));
  EXPECT_THAT(after->entries(), ElementsAre(10));
  EXPECT_THAT(existing->use_count(), Eq(4));
  EXPECT_THAT(added->use_count(), Eq(3));
  Node::Release(source, existing);
  Node::Release(source, added);
  Node::Release(source, original);
  Node::Release(source, before);
  EXPECT_THAT(after->children().front()->use_count(), Eq(1));
  Node::Release(source, after);
  EXPECT_THAT(source.released, Eq(5));
}

TEST_F(HamtSharedNodeTest, RejectsInsertionThatChangesUnrelatedPayloadCounts) {
  CountingSource source;
  auto* const original = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  auto* const child = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  ASSERT_THAT(child, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  EXPECT_THAT(Node::TryInsertEntry(source, *original, index, 0, 10), Eq(std::nullopt));
  EXPECT_THAT(Node::TryInsertChild(source, *original, index, 0, child), Eq(std::nullopt));
  EXPECT_THAT(original->entries(), IsEmpty());
  EXPECT_THAT(original->children(), IsEmpty());
  EXPECT_THAT(child->use_count(), Eq(1));
  EXPECT_THAT(source.acquired, Eq(2));
  Node::Release(source, original);
  Node::Release(source, child);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, RejectsChildInsertionIntoTerminalCollisions) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({10, 20});
  auto* const original = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  auto* const child = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  ASSERT_THAT(child, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  EXPECT_THAT(Node::TryInsertChild(source, *original, index, 0, child), Eq(std::nullopt));
  EXPECT_THAT(original->entries(), ElementsAre(10, 20));
  EXPECT_THAT(child->use_count(), Eq(1));
  EXPECT_THAT(source.acquired, Eq(2));
  Node::Release(source, original);
  Node::Release(source, child);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, FailedChildInsertionDoesNotRetainTheChild) {
  CountingSource source;
  auto* const original = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  auto* const child = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  ASSERT_THAT(child, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  EXPECT_THAT(Node::TryInsertChild(source, *original, index, 0, nullptr), Eq(std::nullopt));
  EXPECT_THAT(Node::TryInsertChild(source, *original, {}, 0, child), Eq(std::nullopt));
  EXPECT_THAT(Node::TryInsertChild(source, *original, index, 1, child), Eq(std::nullopt));
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryInsertChild(exhausted, *original, index, 0, child), Eq(std::nullopt));
  EXPECT_THAT(child->use_count(), Eq(1));
  EXPECT_THAT(original->children().empty(), Eq(true));
  EXPECT_THAT(source.acquired, Eq(2));
  Node::Release(source, original);
  Node::Release(source, child);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, ReplacementRetainsOnlyTheNewChildForItsCopiedNode) {
  CountingSource source;
  auto* const old_child = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  auto* const replacement = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(old_child, NotNull());
  ASSERT_THAT(replacement, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  constexpr auto kEntries = std::to_array<int>({10});
  const auto children = std::to_array<Node*>({old_child});
  auto* const original = Node::TryCreate(source, index, kEntries, children).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  auto* const copied = Node::TryReplaceChild(source, *original, 0, replacement).value_or(nullptr);
  ASSERT_THAT(copied, NotNull());
  EXPECT_THAT(original->children(), ElementsAre(old_child));
  EXPECT_THAT(copied->children(), ElementsAre(replacement));
  EXPECT_THAT(copied->entries(), ElementsAre(10));
  EXPECT_THAT(old_child->use_count(), Eq(2));
  EXPECT_THAT(replacement->use_count(), Eq(2));
  Node::Release(source, old_child);
  Node::Release(source, replacement);
  Node::Release(source, original);
  EXPECT_THAT(source.released, Eq(2));
  EXPECT_THAT(copied->children().front()->use_count(), Eq(1));
  Node::Release(source, copied);
  EXPECT_THAT(source.released, Eq(4));
}

TEST_F(HamtSharedNodeTest, SelfReplacementCreatesAnotherOwnerOfTheSameChild) {
  CountingSource source;
  auto* const child = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(child, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  const auto children = std::to_array<Node*>({child});
  auto* const original = Node::TryCreate(source, index, {}, children).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  auto* const copied = Node::TryReplaceChild(source, *original, 0, child).value_or(nullptr);
  ASSERT_THAT(copied, NotNull());
  EXPECT_THAT(child->use_count(), Eq(3));
  Node::Release(source, original);
  Node::Release(source, child);
  EXPECT_THAT(copied->children().front()->use_count(), Eq(1));
  Node::Release(source, copied);
  EXPECT_THAT(source.released, Eq(3));
}

TEST_F(HamtSharedNodeTest, RejectsChildReplacementInTerminalCollisions) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({10, 20});
  auto* const original = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  auto* const replacement = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  ASSERT_THAT(replacement, NotNull());
  EXPECT_THAT(Node::TryReplaceChild(source, *original, 0, replacement), Eq(std::nullopt));
  EXPECT_THAT(original->entries(), ElementsAre(10, 20));
  EXPECT_THAT(replacement->use_count(), Eq(1));
  EXPECT_THAT(source.acquired, Eq(2));
  Node::Release(source, original);
  Node::Release(source, replacement);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, FailedReplacementLeavesBothChildrenUnchanged) {
  CountingSource source;
  auto* const child = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(child, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  const auto children = std::to_array<Node*>({child});
  auto* const original = Node::TryCreate(source, index, {}, children).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  EXPECT_THAT(Node::TryReplaceChild(source, *original, 0, nullptr), Eq(std::nullopt));
  EXPECT_THAT(Node::TryReplaceChild(source, *original, 1, child), Eq(std::nullopt));
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryReplaceChild(exhausted, *original, 0, child), Eq(std::nullopt));
  EXPECT_THAT(child->use_count(), Eq(2));
  EXPECT_THAT(original->children(), ElementsAre(child));
  Node::Release(source, original);
  Node::Release(source, child);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, ErasesEachDenseEntryWithoutChangingOriginal) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertData(2), Eq(true));
  ASSERT_THAT(index.InsertData(3), Eq(true));
  constexpr auto kEntries = std::to_array<int>({10, 20, 30});
  auto* const original = Node::TryCreate(source, index, kEntries, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  for (std::size_t position = 0; position < kEntries.size(); ++position) {
    auto erased_index = index;
    ASSERT_THAT(erased_index.EraseData(position + 1), Eq(true));
    auto* const erased = Node::TryEraseEntry(source, *original, erased_index, position).value_or(nullptr);
    ASSERT_THAT(erased, NotNull());
    const int* expected = kEntries.data();
    for (const int entry : erased->entries()) {
      if (expected == kEntries.data() + position) {
        ++expected;
      }
      EXPECT_THAT(entry, Eq(*expected++));
    }
    EXPECT_THAT(erased->entries(), SizeIs(2));
    EXPECT_THAT(original->entries(), ElementsAre(10, 20, 30));
    Node::Release(source, erased);
  }
  Node::Release(source, original);
  EXPECT_THAT(source.released, Eq(4));
}

TEST_F(HamtSharedNodeTest, ErasingTheLastEntryRetainsItsSharedChildren) {
  CountingSource source;
  auto* const child = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(child, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  constexpr auto kEntries = std::to_array<int>({10});
  const auto children = std::to_array<Node*>({child});
  auto* const original = Node::TryCreate(source, index, kEntries, children).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  ASSERT_THAT(index.EraseData(1), Eq(true));
  auto* const erased = Node::TryEraseEntry(source, *original, index, 0).value_or(nullptr);
  ASSERT_THAT(erased, NotNull());
  EXPECT_THAT(erased->entries(), IsEmpty());
  EXPECT_THAT(child->use_count(), Eq(3));
  Node::Release(source, original);
  Node::Release(source, child);
  EXPECT_THAT(erased->children().front()->use_count(), Eq(1));
  Node::Release(source, erased);
  EXPECT_THAT(source.released, Eq(3));
}

TEST_F(HamtSharedNodeTest, FailedErasurePreservesEntries) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  constexpr auto kEntries = std::to_array<int>({10});
  auto* const original = Node::TryCreate(source, index, kEntries, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  EXPECT_THAT(Node::TryEraseEntry(source, *original, index, 0), Eq(std::nullopt));
  EXPECT_THAT(Node::TryEraseEntry(source, *original, {}, 1), Eq(std::nullopt));
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryEraseEntry(exhausted, *original, {}, 0), Eq(std::nullopt));
  EXPECT_THAT(original->entries(), ElementsAre(10));
  EXPECT_THAT(source.acquired, Eq(1));
  Node::Release(source, original);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, RejectsEntryErasureFromCollisionsAndEmptyNodes) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({10, 20});
  auto* const collision = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  auto* const empty = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(collision, NotNull());
  ASSERT_THAT(empty, NotNull());
  EXPECT_THAT(Node::TryEraseEntry(source, *collision, {}, 0), Eq(std::nullopt));
  EXPECT_THAT(Node::TryEraseEntry(source, *empty, {}, 0), Eq(std::nullopt));
  EXPECT_THAT(collision->entries(), ElementsAre(10, 20));
  EXPECT_THAT(source.acquired, Eq(2));
  Node::Release(source, collision);
  Node::Release(source, empty);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, RejectsEntryErasureThatChangesChildOccupancy) {
  CountingSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  constexpr auto kEntries = std::to_array<int>({10});
  auto* const original = Node::TryCreate(source, index, kEntries, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  Node::index_type invalid_index;
  ASSERT_THAT(invalid_index.InsertNode(2), Eq(true));
  EXPECT_THAT(Node::TryEraseEntry(source, *original, invalid_index, 0), Eq(std::nullopt));
  EXPECT_THAT(original->entries(), ElementsAre(10));
  EXPECT_THAT(source.acquired, Eq(1));
  Node::Release(source, original);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, ErasesEachChildWhilePreservingTheOriginalOwners) {
  CountingSource source;
  std::array<Node*, 3> children{};
  Node::index_type index;
  std::size_t fragment = 1;
  for (Node*& child : children) {
    child = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
    ASSERT_THAT(child, NotNull());
    ASSERT_THAT(index.InsertNode(fragment++), Eq(true));
  }
  ASSERT_THAT(index.InsertData(7), Eq(true));
  constexpr auto kEntries = std::to_array<int>({70});
  auto* const original = Node::TryCreate(source, index, kEntries, children).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  for (std::size_t position = 0; position < children.size(); ++position) {
    auto erased_index = index;
    ASSERT_THAT(erased_index.EraseNode(position + 1), Eq(true));
    auto* const erased = Node::TryEraseChild(source, *original, erased_index, position).value_or(nullptr);
    ASSERT_THAT(erased, NotNull());
    EXPECT_THAT(erased->entries(), ElementsAre(70));
    ASSERT_THAT(erased->children(), SizeIs(2));
    EXPECT_THAT(children.at(position)->use_count(), Eq(2));
    Node* const * expected = children.data();
    for (const Node* const remaining : erased->children()) {
      if (expected == children.data() + position) {
        ++expected;
      }
      EXPECT_THAT(remaining, Eq(*expected++));
      EXPECT_THAT(remaining->use_count(), Eq(3));
    }
    Node::Release(source, erased);
    EXPECT_THAT(original->children(), ElementsAreArray(children));
  }
  Node::Release(source, original);
  for (Node* child : children) {
    EXPECT_THAT(child->use_count(), Eq(1));
    Node::Release(source, child);
  }
  EXPECT_THAT(source.released, Eq(7));
}

TEST_F(HamtSharedNodeTest, ErasesTheOnlyChildIntoAnEmptyNode) {
  CountingSource source;
  auto* const child = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(child, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(1), Eq(true));
  const auto children = std::to_array<Node*>({child});
  auto* const original = Node::TryCreate(source, index, {}, children).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  auto* const erased = Node::TryEraseChild(source, *original, {}, 0).value_or(nullptr);
  ASSERT_THAT(erased, NotNull());
  EXPECT_THAT(erased->children(), IsEmpty());
  EXPECT_THAT(erased->entries(), IsEmpty());
  EXPECT_THAT(child->use_count(), Eq(2));
  Node::Release(source, original);
  Node::Release(source, child);
  Node::Release(source, erased);
  EXPECT_THAT(source.released, Eq(3));
}

TEST_F(HamtSharedNodeTest, FailedChildErasurePreservesReferenceCounts) {
  CountingSource source;
  auto* const child = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(child, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(1), Eq(true));
  const auto children = std::to_array<Node*>({child});
  auto* const original = Node::TryCreate(source, index, {}, children).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  EXPECT_THAT(Node::TryEraseChild(source, *original, index, 0), Eq(std::nullopt));
  EXPECT_THAT(Node::TryEraseChild(source, *original, {}, 1), Eq(std::nullopt));
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryEraseChild(exhausted, *original, {}, 0), Eq(std::nullopt));
  EXPECT_THAT(child->use_count(), Eq(2));
  EXPECT_THAT(original->children(), ElementsAre(child));
  Node::Release(source, original);
  Node::Release(source, child);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, RejectsChildErasureFromCollisionsAndEmptyNodes) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({10, 20});
  auto* const collision = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  auto* const empty = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(collision, NotNull());
  ASSERT_THAT(empty, NotNull());
  EXPECT_THAT(Node::TryEraseChild(source, *collision, {}, 0), Eq(std::nullopt));
  EXPECT_THAT(Node::TryEraseChild(source, *empty, {}, 0), Eq(std::nullopt));
  EXPECT_THAT(collision->entries(), ElementsAre(10, 20));
  EXPECT_THAT(source.acquired, Eq(2));
  Node::Release(source, collision);
  Node::Release(source, empty);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, RejectsChildErasureThatChangesEntryOccupancy) {
  CountingSource source;
  auto* const child = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(child, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(1), Eq(true));
  const auto children = std::to_array<Node*>({child});
  auto* const original = Node::TryCreate(source, index, {}, children).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  Node::index_type invalid_index;
  ASSERT_THAT(invalid_index.InsertData(2), Eq(true));
  EXPECT_THAT(Node::TryEraseChild(source, *original, invalid_index, 0), Eq(std::nullopt));
  EXPECT_THAT(original->children(), ElementsAre(child));
  EXPECT_THAT(child->use_count(), Eq(2));
  EXPECT_THAT(source.acquired, Eq(2));
  Node::Release(source, original);
  Node::Release(source, child);
  EXPECT_THAT(source.released, Eq(2));
}

TEST_F(HamtSharedNodeTest, CollisionInsertionPreservesOrderAtEveryPosition) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({10, 30});
  auto* const original = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  for (std::size_t position = 0; position <= kEntries.size(); ++position) {
    SCOPED_TRACE(position);
    auto* const inserted = Node::TryInsertCollisionEntry(source, *original, position, 20).value_or(nullptr);
    ASSERT_THAT(inserted, NotNull());
    EXPECT_THAT(inserted->is_collision(), Eq(true));
    EXPECT_THAT(inserted->children(), IsEmpty());
    ASSERT_THAT(inserted->entries(), SizeIs(3));
    const int* expected = kEntries.data();
    std::size_t rank = 0;
    for (const int entry : inserted->entries()) {
      EXPECT_THAT(entry, Eq(rank++ == position ? 20 : *expected++));
    }
    EXPECT_THAT(original->entries(), ElementsAre(10, 30));
    Node::Release(source, inserted);
  }
  Node::Release(source, original);
  EXPECT_THAT(source.released, Eq(4));
}

TEST_F(HamtSharedNodeTest, CollisionErasureCanLeaveASingleEntry) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({10, 20});
  auto* const original = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  for (std::size_t position = 0; position < kEntries.size(); ++position) {
    SCOPED_TRACE(position);
    auto* const erased = Node::TryEraseCollisionEntry(source, *original, position).value_or(nullptr);
    ASSERT_THAT(erased, NotNull());
    EXPECT_THAT(erased->is_collision(), Eq(true));
    EXPECT_THAT(erased->entries(), ElementsAre(kEntries.at(1 - position)));
    EXPECT_THAT(Node::TryEraseCollisionEntry(source, *erased, 0), Eq(std::nullopt));
    EXPECT_THAT(original->entries(), ElementsAre(10, 20));
    Node::Release(source, erased);
  }
  Node::Release(source, original);
  EXPECT_THAT(source.released, Eq(3));
}

TEST_F(HamtSharedNodeTest, CollisionMutationFailureDoesNotChangeOriginal) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({10, 20});
  auto* const original = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  EXPECT_THAT(Node::TryInsertCollisionEntry(source, *original, 3, 30), Eq(std::nullopt));
  EXPECT_THAT(Node::TryEraseCollisionEntry(source, *original, 2), Eq(std::nullopt));
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryInsertCollisionEntry(exhausted, *original, 2, 30), Eq(std::nullopt));
  EXPECT_THAT(Node::TryEraseCollisionEntry(exhausted, *original, 0), Eq(std::nullopt));
  EXPECT_THAT(original->entries(), ElementsAre(10, 20));
  EXPECT_THAT(original->use_count(), Eq(1));
  EXPECT_THAT(source.acquired, Eq(1));
  Node::Release(source, original);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, ReplacesCollisionEntriesAtEveryPositionWithoutChangingOriginal) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({10, 20, 30});
  auto* const original = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  for (std::size_t position = 0; position < kEntries.size(); ++position) {
    SCOPED_TRACE(position);
    auto* const replaced = Node::TryReplaceEntry(source, *original, position, 99).value_or(nullptr);
    ASSERT_THAT(replaced, NotNull());
    EXPECT_THAT(replaced->is_collision(), Eq(true));
    ASSERT_THAT(replaced->entries(), SizeIs(kEntries.size()));
    std::size_t rank = 0;
    for (const int entry : replaced->entries()) {
      EXPECT_THAT(entry, Eq(rank == position ? 99 : kEntries.at(rank)));
      ++rank;
    }
    EXPECT_THAT(original->entries(), ElementsAre(10, 20, 30));
    Node::Release(source, replaced);
  }
  Node::Release(source, original);
  EXPECT_THAT(source.released, Eq(4));
}

TEST_F(HamtSharedNodeTest, BitmapReplacementRetainsChildrenAndSupportsAliasedInput) {
  CountingSource source;
  auto* const child = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(child, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  constexpr auto kEntries = std::to_array<int>({10});
  const auto children = std::to_array<Node*>({child});
  auto* const original = Node::TryCreate(source, index, kEntries, children).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  auto* const replaced = Node::TryReplaceEntry(source, *original, 0, original->entries().front()).value_or(nullptr);
  ASSERT_THAT(replaced, NotNull());
  EXPECT_THAT(replaced->is_collision(), Eq(false));
  EXPECT_THAT(replaced->entries(), ElementsAre(10));
  EXPECT_THAT(child->use_count(), Eq(3));
  Node::Release(source, child);
  Node::Release(source, original);
  EXPECT_THAT(replaced->children().front()->use_count(), Eq(1));
  Node::Release(source, replaced);
  EXPECT_THAT(source.released, Eq(3));
}

TEST_F(HamtSharedNodeTest, EntryReplacementFailurePreservesOriginal) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({10});
  auto* const original = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  EXPECT_THAT(Node::TryReplaceEntry(source, *original, 1, 20), Eq(std::nullopt));
  mbo::memory::FixedBlockSource exhausted(std::span<std::byte>{});
  EXPECT_THAT(Node::TryReplaceEntry(exhausted, *original, 0, 20), Eq(std::nullopt));
  EXPECT_THAT(original->entries(), ElementsAre(10));
  EXPECT_THAT(original->use_count(), Eq(1));
  EXPECT_THAT(source.acquired, Eq(1));
  Node::Release(source, original);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, CollisionMutationRejectsNormalNodes) {
  CountingSource source;
  auto* const original = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  EXPECT_THAT(Node::TryInsertCollisionEntry(source, *original, 0, 10), Eq(std::nullopt));
  EXPECT_THAT(Node::TryEraseCollisionEntry(source, *original, 0), Eq(std::nullopt));
  EXPECT_THAT(original->entries(), IsEmpty());
  EXPECT_THAT(source.acquired, Eq(1));
  Node::Release(source, original);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtSharedNodeTest, CollisionInsertionCanCopyAnAliasedEntry) {
  CountingSource source;
  constexpr auto kEntries = std::to_array<int>({10, 20});
  auto* const original = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  auto* const inserted =
      Node::TryInsertCollisionEntry(source, *original, 1, original->entries().front()).value_or(nullptr);
  ASSERT_THAT(inserted, NotNull());
  EXPECT_THAT(inserted->entries(), ElementsAre(10, 10, 20));
  EXPECT_THAT(original->entries(), ElementsAre(10, 20));
  Node::Release(source, original);
  EXPECT_THAT(inserted->entries(), ElementsAre(10, 10, 20));
  Node::Release(source, inserted);
  EXPECT_THAT(source.released, Eq(2));
}

}  // namespace
}  // namespace mbo::container::container_internal
