// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_node_collision.h"

#include <array>
#include <cstddef>
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
  constexpr bool operator()(int lhs, long rhs) const noexcept { return lhs == rhs; }

  constexpr bool operator()(int lhs, int rhs) const noexcept { return lhs == rhs; }
};

struct HamtNodeCollisionTest : ::testing::Test {};

TEST_F(HamtNodeCollisionTest, PreservesOtherEntryAddressesAcrossMutation) {
  HamtNodeCollisionBucket<int, Identity, Equal> bucket;
  const auto first = bucket.try_insert(7, 11);
  const auto second = bucket.try_insert(7, 13);
  ASSERT_THAT(first.entry, NotNull());
  ASSERT_THAT(second.entry, NotNull());
  const auto* const second_address = second.entry;

  EXPECT_THAT(bucket.try_insert(7, 11).inserted, Eq(false));
  EXPECT_THAT(bucket.find(7, 13L), Eq(second_address));
  EXPECT_THAT(bucket.erase(7, 11L), Eq(true));
  EXPECT_THAT(bucket.find(7, 13L), Eq(second_address));
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
