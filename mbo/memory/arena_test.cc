// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/memory/arena.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <utility>
#include <vector>

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

// NOLINTBEGIN(readability-identifier-naming): test doubles model BlockSource spelling.
struct RecordingSource final {
  static constexpr bool supports_recoverable_failure = true;

  static constexpr std::size_t max_alignment() noexcept { return NewDeleteBlockSource::max_alignment(); }

  std::optional<MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    requested_sizes.push_back(size);
    return NewDeleteBlockSource::TryAcquire(size, alignment);
  }

  static void Release(MemoryBlock block) noexcept { NewDeleteBlockSource::Release(block); }

  std::vector<std::size_t> requested_sizes;
};

struct UndersizedSource final {
  static constexpr bool supports_recoverable_failure = true;

  static constexpr std::size_t max_alignment() noexcept { return 64; }

  std::optional<MemoryBlock> TryAcquire(std::size_t size, std::size_t /*alignment*/) noexcept {
    return MemoryBlock{.data = storage.data(), .size = size - 1, .alignment = 64};
  }

  void Release(MemoryBlock /*block*/) noexcept { ++release_count; }

  alignas(64) std::array<std::byte, 2'048> storage{};
  std::size_t release_count = 0;
};

// NOLINTEND(readability-identifier-naming)

template<typename ArenaType>
concept HasTryAllocate = requires(ArenaType& arena) { arena.TryAllocate(1); };

#if __cpp_exceptions
static_assert(HasTryAllocate<Arena<AllocatorBlockSource<>, kSmallArenaOptions>>);
static_assert(HasTryAllocate<Arena<PmrBlockSource, kSmallArenaOptions>>);
#else
static_assert(!HasTryAllocate<Arena<AllocatorBlockSource<>, kSmallArenaOptions>>);
static_assert(!HasTryAllocate<Arena<PmrBlockSource, kSmallArenaOptions>>);
#endif
static_assert(HasTryAllocate<Arena<NewDeleteBlockSource, kSmallArenaOptions>>);

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

TEST_F(ArenaTest, MoveAssignmentReleasesDestinationAndTransfersSource) {
  Arena<NewDeleteBlockSource, kSmallArenaOptions> source;
  Arena<NewDeleteBlockSource, kSmallArenaOptions> destination;
  auto* const source_allocation = source.TryAllocate(80);
  ASSERT_THAT(source_allocation, NotNull());
  ASSERT_THAT(destination.TryAllocate(80), NotNull());
  const auto source_reserved = source.bytes_reserved();

  destination = std::move(source);

  EXPECT_THAT(destination.bytes_reserved(), Eq(source_reserved));
  EXPECT_THAT(destination.block_count(), Eq(1));
  source_allocation[0] = std::byte{0x2a};
  EXPECT_THAT(source_allocation[0], Eq(std::byte{0x2a}));
}

TEST_F(ArenaTest, SwapTransfersOwnershipWithoutMovingAllocations) {
  Arena<NewDeleteBlockSource, kSmallArenaOptions> first;
  Arena<NewDeleteBlockSource, kSmallArenaOptions> second;
  auto* const first_allocation = first.TryAllocate(80);
  auto* const second_allocation = second.TryAllocate(400);
  ASSERT_THAT(first_allocation, NotNull());
  ASSERT_THAT(second_allocation, NotNull());
  const auto first_blocks = first.block_count();
  const auto second_blocks = second.block_count();

  swap(first, second);

  EXPECT_THAT(first.block_count(), Eq(second_blocks));
  EXPECT_THAT(second.block_count(), Eq(first_blocks));
  first_allocation[0] = std::byte{0x11};
  second_allocation[0] = std::byte{0x22};
  EXPECT_THAT(first_allocation[0], Eq(std::byte{0x11}));
  EXPECT_THAT(second_allocation[0], Eq(std::byte{0x22}));
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

TEST_F(ArenaTest, AllocatorSourceSupportsHardAllocation) {
  Arena<AllocatorBlockSource<>, kSmallArenaOptions> arena;

  auto* const allocation = arena.Allocate(80, alignof(std::max_align_t));

  ASSERT_THAT(allocation, NotNull());
  EXPECT_THAT(arena.block_count(), Eq(1));
}

TEST_F(ArenaTest, PmrSourceSupportsHardAllocation) {
  std::pmr::monotonic_buffer_resource resource;
  Arena<PmrBlockSource, kSmallArenaOptions> arena{PmrBlockSource(&resource)};

  auto* const allocation = arena.Allocate(80, 64);

  ASSERT_THAT(allocation, NotNull());
  EXPECT_THAT(std::bit_cast<std::uintptr_t>(allocation) % 64, Eq(0));
}

TEST_F(ArenaTest, OversizedBlockDoesNotAdvanceNormalGrowth) {
  Arena<RecordingSource, kSmallArenaOptions> arena;
  ASSERT_THAT(arena.TryAllocate(180, 8), NotNull());
  ASSERT_THAT(arena.TryAllocate(1'000, 8), NotNull());
  ASSERT_THAT(arena.TryAllocate(400, 8), NotNull());

  ASSERT_THAT(arena.source().requested_sizes.size(), Eq(3));
  EXPECT_THAT(arena.source().requested_sizes.at(0), Eq(256));
  EXPECT_THAT(arena.source().requested_sizes.at(1), Ge(1'000));
  EXPECT_THAT(arena.source().requested_sizes.at(2), Eq(512));
}

TEST_F(ArenaTest, InvalidSourceResponseIsReleasedWithoutMutation) {
  Arena<UndersizedSource, kSmallArenaOptions> arena;

  EXPECT_THAT(arena.TryAllocate(80, 16), IsNull());

  EXPECT_THAT(arena.bytes_used(), Eq(0));
  EXPECT_THAT(arena.bytes_reserved(), Eq(0));
  EXPECT_THAT(arena.block_count(), Eq(0));
  EXPECT_THAT(arena.source().release_count, Eq(1));
}

}  // namespace
}  // namespace mbo::memory
