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
using ::testing::IsEmpty;

struct Header final {
  std::uint64_t bitmap;
};

using Source = mbo::memory::InlineBlockSource<256, alignof(std::max_align_t)>;
using Block = HamtPackedNodeBlock<Header, int, const void*, Source>;

struct HamtPackedNodeBlockTest : ::testing::Test {};

TEST_F(HamtPackedNodeBlockTest, OwnsOneAlignedBlockWithDenseArrays) {
  Source source;
  Block block(source);
  constexpr auto kEntries = std::to_array<int>({3, 5, 7});
  const auto children = std::to_array<const void*>({&source, &block});

  ASSERT_THAT(block.TryInitialize(Header{.bitmap = 42}, kEntries, children), Eq(true));
  EXPECT_THAT(block.header().bitmap, Eq(42));
  EXPECT_THAT(block.entries(), ElementsAre(3, 5, 7));
  EXPECT_THAT(block.children(), ElementsAre(&source, &block));
  EXPECT_THAT(block.TryInitialize(Header{}, {}, {}), Eq(false));

  block.clear();
  EXPECT_THAT(block.empty(), Eq(true));
  EXPECT_THAT(block.TryInitialize(Header{.bitmap = 9}, {}, {}), Eq(true));
  EXPECT_THAT(block.header().bitmap, Eq(9));
}

TEST_F(HamtPackedNodeBlockTest, EmptyAndClearedBlocksExposeEmptySpans) {
  Source source;
  Block block(source);
  const auto& const_block = block;
  EXPECT_THAT(block.entries(), IsEmpty());
  EXPECT_THAT(block.children(), IsEmpty());
  EXPECT_THAT(const_block.entries(), IsEmpty());
  EXPECT_THAT(const_block.children(), IsEmpty());
  ASSERT_THAT(block.TryInitialize(Header{}, {}, {}), Eq(true));
  block.clear();
  block.clear();
  EXPECT_THAT(const_block.entries(), IsEmpty());
  EXPECT_THAT(const_block.children(), IsEmpty());
}

TEST_F(HamtPackedNodeBlockTest, HeaderAccessRequiresInitialization) {
  Source source;
  Block block(source);
  const auto& const_block = block;
  EXPECT_DEATH((void)block.header(), "requires an initialized block");
  EXPECT_DEATH((void)const_block.header(), "requires an initialized block");
}

TEST_F(HamtPackedNodeBlockTest, SourceExhaustionLeavesBlockEmptyAndDestructionReleasesStorage) {
  Source source;
  Block waiting(source);
  {
    Block owner(source);
    ASSERT_THAT(owner.TryInitialize(Header{}, {}, {}), Eq(true));
    EXPECT_THAT(waiting.TryInitialize(Header{}, {}, {}), Eq(false));
    EXPECT_THAT(waiting.empty(), Eq(true));
  }
  EXPECT_THAT(waiting.TryInitialize(Header{}, {}, {}), Eq(true));
}

}  // namespace
}  // namespace mbo::container::container_internal
