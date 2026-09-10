// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/memory/arena.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::memory {
namespace {

using ::testing::Eq;
using ::testing::Ge;
using ::testing::IsNull;
using ::testing::Ne;
using ::testing::NotNull;

inline constexpr ArenaOptions kSmallArenaOptions{
    .initial_block_size = 256,
    .maximum_block_size = 1'024,
    .growth_numerator = 2,
    .growth_denominator = 1,
};

struct ArenaTest : ::testing::Test {};

TEST_F(ArenaTest, AllocationsAreAlignedAndStable) {
  Arena<NewDeleteBlockSource, kSmallArenaOptions> arena;

  auto* const first = arena.TryAllocate(13, 8);
  auto* const second = arena.TryAllocate(31, 32);

  ASSERT_THAT(first, NotNull());
  ASSERT_THAT(second, NotNull());
  EXPECT_THAT(std::bit_cast<std::uintptr_t>(first) % 8, Eq(0));
  EXPECT_THAT(std::bit_cast<std::uintptr_t>(second) % 32, Eq(0));
  first[0] = std::byte{0x5a};
  EXPECT_THAT(first[0], Eq(std::byte{0x5a}));
  EXPECT_THAT(arena.bytes_used(), Ge(44));
  EXPECT_THAT(arena.block_count(), Ge(1));
  EXPECT_THAT(arena.bytes_reserved(), Ge(kSmallArenaOptions.initial_block_size));
}

TEST_F(ArenaTest, FixedSourceFailureIsTransactional) {
  alignas(64) std::array<std::byte, 512> storage{};
  Arena<FixedBlockSource, kSmallArenaOptions> arena(FixedBlockSource(std::span<std::byte>(storage), 64));
  ASSERT_THAT(arena.TryAllocate(64, 64), NotNull());
  const auto used = arena.bytes_used();
  const auto reserved = arena.bytes_reserved();
  const auto blocks = arena.block_count();

  EXPECT_THAT(arena.TryAllocate(1'024), IsNull());
  EXPECT_THAT(arena.bytes_used(), Eq(used));
  EXPECT_THAT(arena.bytes_reserved(), Eq(reserved));
  EXPECT_THAT(arena.block_count(), Eq(blocks));
}

TEST_F(ArenaTest, ResetRetainsAndReusesBlocks) {
  Arena<NewDeleteBlockSource, kSmallArenaOptions> arena;
  auto* const before = arena.TryAllocate(80, 16);
  ASSERT_THAT(before, NotNull());
  const auto reserved = arena.bytes_reserved();
  const auto blocks = arena.block_count();

  arena.Reset();
  auto* const after = arena.TryAllocate(80, 16);

  EXPECT_THAT(after, Eq(before));
  EXPECT_THAT(arena.bytes_reserved(), Eq(reserved));
  EXPECT_THAT(arena.block_count(), Eq(blocks));
}

TEST_F(ArenaTest, ReleaseReturnsAllStorageAndRestoresInitialState) {
  Arena<NewDeleteBlockSource, kSmallArenaOptions> arena;
  ASSERT_THAT(arena.TryAllocate(400), NotNull());
  ASSERT_THAT(arena.block_count(), Ne(0));

  arena.Release();

  EXPECT_THAT(arena.bytes_used(), Eq(0));
  EXPECT_THAT(arena.bytes_reserved(), Eq(0));
  EXPECT_THAT(arena.block_count(), Eq(0));
  EXPECT_THAT(arena.TryAllocate(8), NotNull());
}

TEST_F(ArenaTest, MoveTransfersBlocksWithoutMovingAllocations) {
  Arena<NewDeleteBlockSource, kSmallArenaOptions> source;
  auto* const allocation = source.TryAllocate(80);
  ASSERT_THAT(allocation, NotNull());
  allocation[0] = std::byte{0x2a};

  const Arena<NewDeleteBlockSource, kSmallArenaOptions> destination(std::move(source));

  EXPECT_THAT(allocation[0], Eq(std::byte{0x2a}));
  EXPECT_THAT(destination.block_count(), Eq(1));
}

TEST_F(ArenaTest, UnsupportedAlignmentFailsWithoutMutation) {
  alignas(16) std::array<std::byte, 512> storage{};
  Arena<FixedBlockSource, kSmallArenaOptions> arena(FixedBlockSource(std::span<std::byte>(storage), 16));

  EXPECT_THAT(arena.TryAllocate(1, 32), IsNull());
  EXPECT_THAT(arena.bytes_used(), Eq(0));
  EXPECT_THAT(arena.bytes_reserved(), Eq(0));
  EXPECT_THAT(arena.block_count(), Eq(0));
}

TEST_F(ArenaTest, LaterStricterAlignmentUsesACompatibleBlock) {
  Arena<NewDeleteBlockSource, kSmallArenaOptions> arena;
  ASSERT_THAT(arena.TryAllocate(1, 8), NotNull());

  auto* const aligned = arena.TryAllocate(1, 256);

  ASSERT_THAT(aligned, NotNull());
  EXPECT_THAT(std::bit_cast<std::uintptr_t>(aligned) % 256, Eq(0));
  EXPECT_THAT(arena.block_count(), Eq(2));
}

}  // namespace
}  // namespace mbo::memory
