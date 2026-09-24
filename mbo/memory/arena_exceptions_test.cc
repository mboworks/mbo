// SPDX-FileCopyrightText: Copyright (c) M. Boerger, The MBO Works Authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/config/config.h"
#include "mbo/memory/arena.h"

namespace mbo::memory {
namespace {

using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::NotNull;
using ::testing::ThrowsMessage;

constexpr ArenaOptions kExceptionTestOptions{
    .initial_block_size = 256,
    .maximum_block_size = 1'024,
    .growth_numerator = 2,
    .growth_denominator = 1,
};

struct ArenaExceptionsTest : ::testing::Test {};

// NOLINTBEGIN(readability-identifier-naming): test doubles model BlockSource spelling.
struct ThrowingAcquireSource final {
  static constexpr bool supports_recoverable_failure = true;

  static constexpr std::size_t max_alignment() noexcept { return NewDeleteBlockSource::max_alignment(); }

  std::optional<MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) const {
    if (throw_on_acquire) {
      throw std::runtime_error("test acquisition failure");
    }
    return NewDeleteBlockSource::TryAcquire(size, alignment);
  }

  static void Release(MemoryBlock block) noexcept { NewDeleteBlockSource::Release(block); }

  bool throw_on_acquire = false;
};

struct ThrowingOwnershipSource final {
  static constexpr bool supports_recoverable_failure = true;

  ThrowingOwnershipSource() = default;
  ThrowingOwnershipSource(const ThrowingOwnershipSource&) = delete;
  ThrowingOwnershipSource& operator=(const ThrowingOwnershipSource&) = delete;

  ThrowingOwnershipSource(ThrowingOwnershipSource&& /*other*/) noexcept(false) {}

  ThrowingOwnershipSource& operator=(ThrowingOwnershipSource&& /*other*/) noexcept(false) { return *this; }

  ~ThrowingOwnershipSource() = default;

  static constexpr std::size_t max_alignment() noexcept { return NewDeleteBlockSource::max_alignment(); }

  static std::optional<MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    return NewDeleteBlockSource::TryAcquire(size, alignment);
  }

  static void Release(MemoryBlock block) noexcept { NewDeleteBlockSource::Release(block); }
};

struct UnavailableSource final {
  static constexpr bool supports_recoverable_failure = true;

  static constexpr std::size_t max_alignment() noexcept { return alignof(std::max_align_t); }

  static std::optional<MemoryBlock> TryAcquire(std::size_t /*size*/, std::size_t /*alignment*/) noexcept {
    return std::nullopt;
  }

  static void Release(MemoryBlock /*block*/) noexcept {}
};

// NOLINTEND(readability-identifier-naming)

using ThrowingOwnershipArena = Arena<ThrowingOwnershipSource, kExceptionTestOptions>;
static_assert(!std::is_move_constructible_v<ThrowingOwnershipArena>);
static_assert(!std::is_move_assignable_v<ThrowingOwnershipArena>);
static_assert(!std::is_swappable_v<ThrowingOwnershipArena>);

TEST_F(ArenaExceptionsTest, SourceAcquisitionExceptionPropagatesWithoutMutation) {
  Arena<ThrowingAcquireSource, kExceptionTestOptions> arena;
  auto* const allocation = arena.TryAllocate(80);
  ASSERT_THAT(allocation, NotNull());
  const auto used = arena.bytes_used();
  const auto reserved = arena.bytes_reserved();
  const auto blocks = arena.block_count();
  arena.source().throw_on_acquire = true;

  EXPECT_THAT(
      [&arena] { static_cast<void>(arena.TryAllocate(400)); },
      ThrowsMessage<std::runtime_error>(HasSubstr("test acquisition failure")));

  EXPECT_THAT(arena.bytes_used(), Eq(used));
  EXPECT_THAT(arena.bytes_reserved(), Eq(reserved));
  EXPECT_THAT(arena.block_count(), Eq(blocks));
  allocation[0] = std::byte{0x2a};
  EXPECT_THAT(allocation[0], Eq(std::byte{0x2a}));
}

TEST_F(ArenaExceptionsTest, AllocateReportsExhaustionThroughRequirementPolicy) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  Arena<UnavailableSource, kExceptionTestOptions> arena;

  EXPECT_THAT(
      [&arena] { static_cast<void>(arena.Allocate(80)); },
      ThrowsMessage<std::runtime_error>(HasSubstr("Arena allocation failed")));
}

TEST_F(ArenaExceptionsTest, ZeroSizeReportsPreconditionThroughRequirementPolicy) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  Arena<NewDeleteBlockSource, kExceptionTestOptions> arena;

  EXPECT_THAT(
      [&arena] { static_cast<void>(arena.TryAllocate(0)); },
      ThrowsMessage<std::runtime_error>(HasSubstr("size must be greater than zero")));
}

TEST_F(ArenaExceptionsTest, InvalidAlignmentReportsPreconditionThroughRequirementPolicy) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  Arena<NewDeleteBlockSource, kExceptionTestOptions> arena;

  EXPECT_THAT(
      [&arena] { static_cast<void>(arena.TryAllocate(1, 3)); },
      ThrowsMessage<std::runtime_error>(HasSubstr("alignment must be a nonzero power of two")));
}

TEST_F(ArenaExceptionsTest, ZeroAlignmentReportsPreconditionThroughRequirementPolicy) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  Arena<NewDeleteBlockSource, kExceptionTestOptions> arena;

  EXPECT_THAT(
      [&arena] { static_cast<void>(arena.TryAllocate(1, 0)); },
      ThrowsMessage<std::runtime_error>(HasSubstr("alignment must be a nonzero power of two")));
}

}  // namespace
}  // namespace mbo::memory
