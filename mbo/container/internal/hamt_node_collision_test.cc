// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_node_collision.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::Eq;
using ::testing::NotNull;

struct Identity final {
  constexpr int operator()(int value) const noexcept { return value; }
};

struct Equal final {
  constexpr bool operator()(int lhs, std::int64_t rhs) const noexcept { return lhs == rhs; }
};

struct HamtNodeCollisionTest : ::testing::Test {};

struct SourceState final {
  mbo::memory::MemoryBlock acquired;
  std::size_t releases = 0;
  bool exact_release = true;
};

// NOLINTBEGIN(readability-identifier-naming): test source implements the BlockSource contract.
struct OversizedSource final {
  static constexpr bool supports_recoverable_failure = true;
  std::reference_wrapper<SourceState> state;

  static constexpr std::size_t max_alignment() noexcept { return alignof(std::max_align_t); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    auto block = mbo::memory::NewDeleteBlockSource::TryAcquire(size + 64, alignment);
    if (block) {
      state.get().acquired = *block;
    }
    return block;
  }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    state.get().exact_release = state.get().exact_release && block == state.get().acquired;
    ++state.get().releases;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }
};

// NOLINTEND(readability-identifier-naming)

TEST_F(HamtNodeCollisionTest, ReturnsOriginalOversizedBlockOnErasureAndDestruction) {
  SourceState state;
  {
    HamtNodeCollisionBucket<int, Identity, Equal, HamtOptions{}, std::uint64_t, OversizedSource> bucket(
        OversizedSource{std::ref(state)});
    ASSERT_THAT(bucket.try_insert(7, 11).entry, NotNull());
    EXPECT_THAT(bucket.erase(7, std::int64_t{11}), Eq(true));
    EXPECT_THAT(state.releases, Eq(1));
    ASSERT_THAT(bucket.try_insert(7, 13).entry, NotNull());
  }
  EXPECT_THAT(state.releases, Eq(2));
  EXPECT_THAT(state.exact_release, Eq(true));
}

TEST_F(HamtNodeCollisionTest, PreservesOtherEntryAddressesAcrossMutation) {
  HamtNodeCollisionBucket<int, Identity, Equal> bucket;
  const auto first = bucket.try_insert(7, 11);
  const auto second = bucket.try_insert(7, 13);
  ASSERT_THAT(first.entry, NotNull());
  ASSERT_THAT(second.entry, NotNull());
  const auto* const second_address = second.entry;

  EXPECT_THAT(bucket.try_insert(7, 11).inserted, Eq(false));
  EXPECT_THAT(bucket.find(7, std::int64_t{13}), Eq(second_address));
  EXPECT_THAT(bucket.erase(7, std::int64_t{11}), Eq(true));
  EXPECT_THAT(bucket.find(7, std::int64_t{13}), Eq(second_address));
  EXPECT_THAT(bucket.size(), Eq(1));
}

TEST_F(HamtNodeCollisionTest, ReportsFixedSourceExhaustionWithoutMutation) {
  using Bucket =
      HamtNodeCollisionBucket<int, Identity, Equal, HamtOptions{}, std::uint64_t, mbo::memory::FixedBlockSource>;
  std::array<std::byte, 1> storage{};
  Bucket bucket(mbo::memory::FixedBlockSource(std::span<std::byte>(storage), alignof(std::max_align_t)));

  const auto result = bucket.try_insert(1, 1);

  EXPECT_THAT(result.error, Eq(HamtError::kAllocationExhausted));
  EXPECT_THAT(bucket.empty(), Eq(true));
}

}  // namespace
}  // namespace mbo::container::container_internal
