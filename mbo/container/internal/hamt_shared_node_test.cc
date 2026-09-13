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
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::NotNull;
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
  static constexpr bool supports_recoverable_failure = true;

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

struct OversizedSource final : CountingSource {
  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    block = source.TryAcquire(size + 64, alignment);
    return block;
  }

  void Release(mbo::memory::MemoryBlock released_block) noexcept {
    EXPECT_THAT(block, Optional(Eq(released_block)));
    CountingSource::Release(released_block);
  }

  std::optional<mbo::memory::MemoryBlock> block;
};

TEST_F(HamtSharedNodeTest, ReleasesTheOriginalAllocationMetadata) {
  OversizedSource source;
  auto* const node = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(node, NotNull());
  Node::Release(source, node);
  EXPECT_THAT(source.released, Eq(1));
}

}  // namespace
}  // namespace mbo::container::container_internal
