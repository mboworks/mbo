// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/memory/block_source.h"

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <span>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::memory {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct BlockSourceTest : ::testing::Test {};

// NOLINTBEGIN(readability-identifier-naming): models the standard allocator interface.
template<typename Value>
struct FailingAllocator {
  using value_type = Value;

  Value* allocate(std::size_t /*count*/) { throw std::bad_alloc(); }

  void deallocate(Value* /*data*/, std::size_t /*count*/) noexcept {}
};

// NOLINTEND(readability-identifier-naming)

TEST_F(BlockSourceTest, FixedSourceCanBeReacquiredAfterRelease) {
  alignas(32) std::array<std::byte, 128> storage{};
  FixedBlockSource source(std::span<std::byte>(storage), 32);

  auto first = source.TryAcquire(64, 32);
  ASSERT_THAT(first.has_value(), IsTrue());
  const MemoryBlock first_block = first.value_or(MemoryBlock{});
  EXPECT_THAT(first_block.size, Eq(storage.size()));
  EXPECT_THAT(source.TryAcquire(1, 1).has_value(), IsFalse());

  source.Release(first_block);
  EXPECT_THAT(source.TryAcquire(128, 16).has_value(), IsTrue());
}

TEST_F(BlockSourceTest, FixedSourceRejectsUnsupportedRequestsWithoutConsumption) {
  alignas(16) std::array<std::byte, 64> storage{};
  FixedBlockSource source(std::span<std::byte>(storage), 16);

  EXPECT_THAT(source.TryAcquire(65, 16).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(0, 16).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(1, 0).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(1, 3).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(1, 32).has_value(), IsFalse());
  source.Release(MemoryBlock{});
  EXPECT_THAT(source.TryAcquire(64, 16).has_value(), IsTrue());
}

TEST_F(BlockSourceTest, NewDeleteSourceRejectsInvalidRequests) {
  EXPECT_THAT(NewDeleteBlockSource::TryAcquire(0, 1).has_value(), IsFalse());
  EXPECT_THAT(NewDeleteBlockSource::TryAcquire(1, 0).has_value(), IsFalse());
  EXPECT_THAT(NewDeleteBlockSource::TryAcquire(1, 3).has_value(), IsFalse());
  EXPECT_THAT(NewDeleteBlockSource::TryAcquire(1, NewDeleteBlockSource::max_alignment() + 1).has_value(), IsFalse());
}

TEST_F(BlockSourceTest, InlineSourceIsFixedAndAddressStable) {
  InlineBlockSource<128, 64> source;
  auto block = source.TryAcquire(128, 64);
  ASSERT_THAT(block.has_value(), IsTrue());
  const MemoryBlock acquired_block = block.value_or(MemoryBlock{});
  const auto* const address = acquired_block.data;

  source.Release(acquired_block);
  auto reacquired = source.TryAcquire(1, 8);

  ASSERT_THAT(reacquired.has_value(), IsTrue());
  EXPECT_THAT(reacquired.value_or(MemoryBlock{}).data, Eq(address));
}

TEST_F(BlockSourceTest, InlineSourceRejectsInvalidRequestsWithoutConsumption) {
  InlineBlockSource<128, 64> source;

  EXPECT_THAT(source.TryAcquire(0, 1).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(129, 1).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(1, 0).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(1, 128).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(1, 3).has_value(), IsFalse());
  ASSERT_THAT(source.TryAcquire(1, 1).has_value(), IsTrue());
  EXPECT_THAT(source.TryAcquire(1, 1).has_value(), IsFalse());
  source.Release(MemoryBlock{});
}

TEST_F(BlockSourceTest, AllocatorSourceRoundsStorageToItsValueSize) {
  AllocatorBlockSource source;

  auto block = source.TryAcquire(sizeof(std::max_align_t) + 1, alignof(std::max_align_t));

  ASSERT_THAT(block.has_value(), IsTrue());
  const auto acquired = block.value_or(MemoryBlock{});
  EXPECT_THAT(acquired.size, Eq(2 * sizeof(std::max_align_t)));
  EXPECT_THAT(acquired.alignment, Eq(alignof(std::max_align_t)));
  source.Release(acquired);
  const auto& const_source = source;
  EXPECT_THAT(&const_source.allocator(), Eq(&source.allocator()));
}

TEST_F(BlockSourceTest, AllocatorSourceRejectsInvalidRequestsAndReportsFailure) {
  AllocatorBlockSource source;
  EXPECT_THAT(source.TryAcquire(0, 1).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(1, 0).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(1, 3).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(1, alignof(std::max_align_t) * 2).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(std::numeric_limits<std::size_t>::max(), 1).has_value(), IsFalse());

#if __cpp_exceptions
  AllocatorBlockSource<FailingAllocator<std::max_align_t>> failing;
  EXPECT_THAT(failing.TryAcquire(1, 1).has_value(), IsFalse());
#endif
}

TEST_F(BlockSourceTest, PmrSourceUsesSelectedResource) {
  alignas(64) std::array<std::byte, 256> storage{};
  std::pmr::monotonic_buffer_resource resource(storage.data(), storage.size(), std::pmr::null_memory_resource());
  PmrBlockSource source(&resource);

  auto block = source.TryAcquire(128, 64);

  ASSERT_THAT(block.has_value(), IsTrue());
  const auto acquired = block.value_or(MemoryBlock{});
  EXPECT_THAT(acquired.size, Eq(128));
  EXPECT_THAT(acquired.alignment, Eq(64));
  source.Release(acquired);
  EXPECT_THAT(source.resource(), Eq(&resource));
}

TEST_F(BlockSourceTest, PmrSourceRejectsInvalidRequestsAndReportsFailure) {
  PmrBlockSource null_source(nullptr);
  EXPECT_THAT(null_source.TryAcquire(1, 1).has_value(), IsFalse());

  PmrBlockSource source;
  EXPECT_THAT(source.TryAcquire(0, 1).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(1, 0).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(1, 3).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(1, PmrBlockSource::max_alignment() + 1).has_value(), IsFalse());

#if __cpp_exceptions
  std::pmr::monotonic_buffer_resource exhausted(std::pmr::null_memory_resource());
  PmrBlockSource failing(&exhausted);
  EXPECT_THAT(failing.TryAcquire(1, 1).has_value(), IsFalse());
#endif
}

}  // namespace
}  // namespace mbo::memory
