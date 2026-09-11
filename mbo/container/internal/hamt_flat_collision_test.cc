// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_flat_collision.h"

#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

struct HamtFlatCollisionTest : ::testing::Test {};

TEST_F(HamtFlatCollisionTest, InsertsFindsDeduplicatesAndErasesFullHashCollisions) {
  HamtFlatCollisionBucket<int, Identity, Equal> bucket;

  const auto first = bucket.try_insert(7, 11);
  const auto second = bucket.try_insert(7, 13);
  const auto duplicate = bucket.try_insert(7, 11);

  ASSERT_THAT(first.entry, NotNull());
  ASSERT_THAT(second.entry, NotNull());
  EXPECT_THAT(first.inserted, Eq(true));
  EXPECT_THAT(second.inserted, Eq(true));
  EXPECT_THAT(duplicate.inserted, Eq(false));
  EXPECT_THAT(bucket.find(7, 13L)->value, Eq(13));
  EXPECT_THAT(bucket.erase(7, 11L), Eq(true));
  EXPECT_THAT(bucket.erase(7, 11L), Eq(false));
  EXPECT_THAT(bucket.size(), Eq(1));
  EXPECT_THAT(bucket.begin()->value, Eq(13));
}

TEST_F(HamtFlatCollisionTest, ReportsMaximumSizeWithoutMutation) {
  constexpr HamtOptions kOneEntry{.maximum_size = 1};
  HamtFlatCollisionBucket<int, Identity, Equal, kOneEntry> bucket;
  ASSERT_THAT(bucket.try_insert(1, 1).entry, NotNull());

  const auto exhausted = bucket.try_insert(1, 2);

  EXPECT_THAT(exhausted.error, Eq(HamtError::kMaxSizeExceeded));
  EXPECT_THAT(bucket.size(), Eq(1));
}

}  // namespace
}  // namespace mbo::container::container_internal
