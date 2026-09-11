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

struct CountingSource final {
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
  ASSERT_THAT(leaf_index.insert_data(3), Eq(true));
  constexpr std::array leaf_entries = {42};
  const auto leaf = Node::TryCreate(source, leaf_index, leaf_entries, std::span<Node* const>{});
  ASSERT_THAT(leaf, Optional(_));

  Node::index_type parent_index;
  ASSERT_THAT(parent_index.insert_node(7), Eq(true));
  const std::array<Node*, 1> children = {*leaf};
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
  ASSERT_THAT(index.insert_data(1), Eq(true));

  EXPECT_THAT(Node::TryCreate(source, index, std::span<const int>{}, std::span<Node* const>{}), Eq(std::nullopt));
  EXPECT_THAT(source.acquired, Eq(0));
}

}  // namespace
}  // namespace mbo::container::container_internal
