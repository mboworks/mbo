// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/memory/arena_block_source.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/arena.h"
#include "mbo/memory/block_source.h"

namespace mbo::memory {
namespace {

using ::testing::Eq;
using ::testing::NotNull;

struct ArenaBlockSourceTest : ::testing::Test {};

TEST_F(ArenaBlockSourceTest, ReusesReleasedBlocksWithoutConsumingMoreArenaCapacity) {
  alignas(std::max_align_t) std::array<std::byte, 1'024> storage{};
  using FixedArena = Arena<FixedBlockSource, ArenaOptions{.initial_block_size = 512, .maximum_block_size = 512}>;
  FixedArena arena{FixedBlockSource(std::span<std::byte>(storage))};
  ArenaBlockSource source(arena);

  const auto first = source.TryAcquire(17, alignof(std::uint64_t));
  ASSERT_THAT(first.has_value(), Eq(true));
  const MemoryBlock first_block = first.value_or(MemoryBlock{});
  ASSERT_THAT(first_block.data, NotNull());
  EXPECT_THAT(first_block.size, Eq(32));
  const auto used = arena.bytes_used();

  source.Release(first_block);
  const auto reused = source.TryAcquire(24, alignof(std::uint64_t));
  ASSERT_THAT(reused.has_value(), Eq(true));
  EXPECT_THAT(reused.value_or(MemoryBlock{}).data, Eq(first_block.data));
  EXPECT_THAT(arena.bytes_used(), Eq(used));
}

TEST_F(ArenaBlockSourceTest, SeparatesSizeClassesAndRejectsUnsupportedRequests) {
  alignas(std::max_align_t) std::array<std::byte, 2'048> storage{};
  using FixedArena = Arena<FixedBlockSource, ArenaOptions{.initial_block_size = 1'024, .maximum_block_size = 1'024}>;
  FixedArena arena{FixedBlockSource(std::span<std::byte>(storage))};
  using Source =
      ArenaBlockSource<FixedArena, ArenaBlockSourceOptions{.minimum_block_size = 32, .maximum_block_size = 128}>;
  Source source(arena);

  const auto small = source.TryAcquire(1, 1);
  const auto large = source.TryAcquire(33, alignof(std::max_align_t));
  ASSERT_THAT(small.has_value(), Eq(true));
  ASSERT_THAT(large.has_value(), Eq(true));
  EXPECT_THAT(small.value_or(MemoryBlock{}).size, Eq(32));
  EXPECT_THAT(large.value_or(MemoryBlock{}).size, Eq(64));
  EXPECT_THAT(source.TryAcquire(129, 1), Eq(std::nullopt));
  EXPECT_THAT(source.TryAcquire(0, 1), Eq(std::nullopt));
  EXPECT_THAT(source.TryAcquire(1, 3), Eq(std::nullopt));
  EXPECT_THAT(source.TryAcquire(1, source.max_alignment() * 2), Eq(std::nullopt));
  EXPECT_THAT(&source.arena(), Eq(&arena));
}

TEST_F(ArenaBlockSourceTest, OptionsRejectInvalidSizesAndAlignments) {
  EXPECT_THAT(ArenaBlockSourceOptions{}.IsValid(), Eq(true));
  EXPECT_THAT(ArenaBlockSourceOptions{.minimum_block_size = 0}.IsValid(), Eq(false));
  EXPECT_THAT(ArenaBlockSourceOptions{.minimum_block_size = 24}.IsValid(), Eq(false));
  EXPECT_THAT(ArenaBlockSourceOptions{.minimum_block_size = 4}.IsValid(), Eq(false));
  EXPECT_THAT((ArenaBlockSourceOptions{.minimum_block_size = 64, .maximum_block_size = 32}.IsValid()), Eq(false));
  EXPECT_THAT(ArenaBlockSourceOptions{.maximum_block_size = 48}.IsValid(), Eq(false));
  EXPECT_THAT(ArenaBlockSourceOptions{.maximum_alignment = 3}.IsValid(), Eq(false));
  EXPECT_THAT(ArenaBlockSourceOptions{.maximum_alignment = 1}.IsValid(), Eq(false));
}

TEST_F(ArenaBlockSourceTest, ReusesEachReleasedBlockAndIgnoresMalformedReleases) {
  alignas(std::max_align_t) std::array<std::byte, 512> storage{};
  using FixedArena = Arena<FixedBlockSource, ArenaOptions{.initial_block_size = 256, .maximum_block_size = 256}>;
  FixedArena arena{FixedBlockSource(std::span<std::byte>(storage))};
  using Source = ArenaBlockSource<
      FixedArena, ArenaBlockSourceOptions{
                      .minimum_block_size = 32, .maximum_block_size = 64, .maximum_alignment = alignof(std::uint64_t)}>;
  Source source(arena);
  EXPECT_THAT(source.max_alignment(), Eq(alignof(std::uint64_t)));

  const MemoryBlock first = source.TryAcquire(32, 1).value_or(MemoryBlock{});
  const MemoryBlock second = source.TryAcquire(32, 1).value_or(MemoryBlock{});
  ASSERT_THAT(first.data, NotNull());
  ASSERT_THAT(second.data, NotNull());
  source.Release(first);
  source.Release(second);
  EXPECT_THAT(source.TryAcquire(32, 1).value_or(MemoryBlock{}).data, Eq(second.data));
  EXPECT_THAT(source.TryAcquire(32, 1).value_or(MemoryBlock{}).data, Eq(first.data));

  const auto used = arena.bytes_used();
  source.Release(MemoryBlock{});
  source.Release(MemoryBlock{.data = first.data, .size = 128, .alignment = alignof(std::uint64_t)});
  source.Release(MemoryBlock{.data = first.data, .size = 48, .alignment = alignof(std::uint64_t)});
  source.Release(MemoryBlock{.data = first.data, .size = 32, .alignment = 1});
  const auto fresh = source.TryAcquire(32, 1);
  ASSERT_THAT(fresh.has_value(), Eq(true));
  EXPECT_THAT(arena.bytes_used(), Eq(used + 32));
}

TEST_F(ArenaBlockSourceTest, ReportsArenaExhaustionWithoutChangingEarlierBlocks) {
  alignas(std::max_align_t) std::array<std::byte, 128> storage{};
  using FixedArena = Arena<FixedBlockSource, ArenaOptions{.initial_block_size = 128, .maximum_block_size = 128}>;
  FixedArena arena{FixedBlockSource(std::span<std::byte>(storage))};
  using Source =
      ArenaBlockSource<FixedArena, ArenaBlockSourceOptions{.minimum_block_size = 32, .maximum_block_size = 32}>;
  Source source(arena);
  const auto first = source.TryAcquire(32, 1);
  const auto second = source.TryAcquire(32, 1);
  ASSERT_THAT(first.has_value(), Eq(true));
  ASSERT_THAT(second.has_value(), Eq(true));
  EXPECT_THAT(source.TryAcquire(32, 1), Eq(std::nullopt));
  EXPECT_THAT(first.value_or(MemoryBlock{}).size, Eq(32));
  EXPECT_THAT(second.value_or(MemoryBlock{}).size, Eq(32));
}

}  // namespace
}  // namespace mbo::memory
