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
  ASSERT_THAT(first->data, NotNull());
  EXPECT_THAT(first->size, Eq(32));
  const auto used = arena.bytes_used();

  source.Release(*first);
  const auto reused = source.TryAcquire(24, alignof(std::uint64_t));
  ASSERT_THAT(reused.has_value(), Eq(true));
  EXPECT_THAT(reused->data, Eq(first->data));
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
  EXPECT_THAT(small->size, Eq(32));
  EXPECT_THAT(large->size, Eq(64));
  EXPECT_THAT(source.TryAcquire(129, 1), Eq(std::nullopt));
  EXPECT_THAT(source.TryAcquire(1, 3), Eq(std::nullopt));
}

}  // namespace
}  // namespace mbo::memory
