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

}  // namespace
}  // namespace mbo::container::container_internal
