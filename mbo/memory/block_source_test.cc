// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/memory/block_source.h"

#include <array>
#include <cstddef>
#include <memory_resource>
#include <span>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::memory {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct BlockSourceTest : ::testing::Test {};

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
  EXPECT_THAT(source.TryAcquire(1, 32).has_value(), IsFalse());
  EXPECT_THAT(source.TryAcquire(64, 16).has_value(), IsTrue());
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

TEST_F(BlockSourceTest, AllocatorSourceRoundsStorageToItsValueSize) {
  AllocatorBlockSource source;

  auto block = source.TryAcquire(sizeof(std::max_align_t) + 1, alignof(std::max_align_t));

  ASSERT_THAT(block.has_value(), IsTrue());
  const auto acquired = block.value_or(MemoryBlock{});
  EXPECT_THAT(acquired.size, Eq(2 * sizeof(std::max_align_t)));
  EXPECT_THAT(acquired.alignment, Eq(alignof(std::max_align_t)));
  source.Release(acquired);
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
}

}  // namespace
}  // namespace mbo::memory
