// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_packed_node_block.h"

#include <array>
#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;

struct Header final {
  std::uint64_t bitmap;
};

using Source = mbo::memory::InlineBlockSource<256, alignof(std::max_align_t)>;
using Block = HamtPackedNodeBlock<Header, int, const void*, Source>;

struct HamtPackedNodeBlockTest : ::testing::Test {};

TEST_F(HamtPackedNodeBlockTest, OwnsOneAlignedBlockWithDenseArrays) {
  Source source;
  Block block(source);
  constexpr std::array entries = {3, 5, 7};
  const std::array<const void*, 2> children = {&source, &block};

  ASSERT_THAT(block.TryInitialize(Header{.bitmap = 42}, entries, children), Eq(true));
  EXPECT_THAT(block.header().bitmap, Eq(42));
  EXPECT_THAT(block.entries(), ElementsAre(3, 5, 7));
  EXPECT_THAT(block.children(), ElementsAre(&source, &block));
  EXPECT_THAT(block.TryInitialize(Header{}, {}, {}), Eq(false));

  block.clear();
  EXPECT_THAT(block.empty(), Eq(true));
  EXPECT_THAT(block.TryInitialize(Header{.bitmap = 9}, {}, {}), Eq(true));
  EXPECT_THAT(block.header().bitmap, Eq(9));
}

}  // namespace
}  // namespace mbo::container::container_internal
