// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/memory/arena.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
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
using ::testing::IsFalse;
using ::testing::IsNull;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::NotNull;

inline constexpr ArenaOptions kSmallArenaOptions{
    .initial_block_size = 256,
    .maximum_block_size = 1'024,
    .growth_numerator = 2,
    .growth_denominator = 1,
};

inline constexpr ArenaOptions kQuotientOverflowOptions{
    .initial_block_size = 128,
    .maximum_block_size = std::numeric_limits<std::size_t>::max(),
    .growth_numerator = std::numeric_limits<std::size_t>::max(),
    .growth_denominator = 1,
};

inline constexpr ArenaOptions kRemainderOverflowOptions{
    .initial_block_size = 255,
    .maximum_block_size = std::numeric_limits<std::size_t>::max(),
    .growth_numerator = std::numeric_limits<std::size_t>::max(),
    .growth_denominator = 128,
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

struct InvalidResponseSource final {
  enum class Response { kUnavailable, kNullData, kInsufficientAlignment, kMisalignedData };

  static constexpr bool supports_recoverable_failure = true;

  static constexpr std::size_t max_alignment() noexcept { return 64; }

  std::optional<MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    switch (response) {
      case Response::kUnavailable: return std::nullopt;
      case Response::kNullData: return MemoryBlock{.data = nullptr, .size = size, .alignment = alignment};
      case Response::kInsufficientAlignment:
        return MemoryBlock{.data = storage.data(), .size = size, .alignment = alignment / 2};
      case Response::kMisalignedData:
        return MemoryBlock{.data = storage.data() + 1, .size = size, .alignment = alignment};
    }
  }

  void Release(MemoryBlock /*block*/) noexcept { ++release_count; }

  alignas(64) std::array<std::byte, 2'048> storage{};
  std::size_t release_count = 0;
  Response response = Response::kUnavailable;
};

struct MaxSizeResponseSource final {
  static constexpr bool supports_recoverable_failure = true;

  static constexpr std::size_t max_alignment() noexcept { return NewDeleteBlockSource::max_alignment(); }

  std::optional<MemoryBlock> TryAcquire(std::size_t /*size*/, std::size_t alignment) noexcept {
    return MemoryBlock{
        .data = storage.data(),
        .size = std::numeric_limits<std::size_t>::max(),
        .alignment = alignment,
    };
  }

  void Release(MemoryBlock /*block*/) noexcept {}

  alignas(64) std::array<std::byte, 256> storage{};
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

TEST_F(ArenaTest, OptionsValidateEveryConstraint) {
  EXPECT_THAT(ArenaOptions{}.IsValid(), IsTrue());
  EXPECT_THAT(ArenaOptions{.initial_block_size = sizeof(void*)}.IsValid(), IsFalse());
  EXPECT_THAT((ArenaOptions{.initial_block_size = 64, .maximum_block_size = 63}.IsValid()), IsFalse());
  EXPECT_THAT((ArenaOptions{.growth_numerator = 1, .growth_denominator = 2}.IsValid()), IsFalse());
  EXPECT_THAT(ArenaOptions{.growth_denominator = 0}.IsValid(), IsFalse());
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

TEST_F(ArenaTest, SelfMoveAssignmentRetainsAllocations) {
  Arena<NewDeleteBlockSource, kSmallArenaOptions> arena;
  auto* const allocation = arena.TryAllocate(80);
  ASSERT_THAT(allocation, NotNull());

  auto* const same = &arena;
  arena = std::move(*same);  // NOLINT(bugprone-use-after-move): exercises the documented self-move guard.

  EXPECT_THAT(arena.block_count(), Eq(1));
  EXPECT_THAT(arena.TryAllocate(1), NotNull());
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

TEST_F(ArenaTest, InvalidSourceResponsesFailWithoutMutation) {
  constexpr std::array kResponses{
      InvalidResponseSource::Response::kUnavailable,
      InvalidResponseSource::Response::kNullData,
      InvalidResponseSource::Response::kInsufficientAlignment,
      InvalidResponseSource::Response::kMisalignedData,
  };
  for (const auto response : kResponses) {
    InvalidResponseSource source;
    source.response = response;
    Arena<InvalidResponseSource, kSmallArenaOptions> arena(source);

    EXPECT_THAT(arena.TryAllocate(80, 16), IsNull());
    EXPECT_THAT(arena.bytes_used(), Eq(0));
    EXPECT_THAT(arena.bytes_reserved(), Eq(0));
    EXPECT_THAT(arena.block_count(), Eq(0));
  }
}

TEST_F(ArenaTest, OverflowingRequestsFailWithoutMutation) {
  Arena<MaxSizeResponseSource, kSmallArenaOptions> arena;
  ASSERT_THAT(arena.TryAllocate(1, 1), NotNull());
  const auto used = arena.bytes_used();
  const auto reserved = arena.bytes_reserved();

  EXPECT_THAT(arena.TryAllocate(std::numeric_limits<std::size_t>::max(), 1), IsNull());
  EXPECT_THAT(arena.bytes_used(), Eq(used));
  EXPECT_THAT(arena.bytes_reserved(), Eq(reserved));

  Arena<MaxSizeResponseSource, kSmallArenaOptions> alignment_arena;
  EXPECT_THAT(alignment_arena.TryAllocate(1, NewDeleteBlockSource::max_alignment()), IsNull());
}

TEST_F(ArenaTest, OverflowingGrowthFallsBackToMaximumBlockSize) {
  Arena<RecordingSource, kQuotientOverflowOptions> quotient;
  ASSERT_THAT(quotient.TryAllocate(1, 1), NotNull());
  ASSERT_THAT(quotient.source().requested_sizes.size(), Eq(1));
  EXPECT_THAT(quotient.source().requested_sizes.front(), Eq(kQuotientOverflowOptions.initial_block_size));

  Arena<RecordingSource, kRemainderOverflowOptions> remainder;
  ASSERT_THAT(remainder.TryAllocate(1, 1), NotNull());
  ASSERT_THAT(remainder.source().requested_sizes.size(), Eq(1));
  EXPECT_THAT(remainder.source().requested_sizes.front(), Eq(kRemainderOverflowOptions.initial_block_size));
}

}  // namespace
}  // namespace mbo::memory
