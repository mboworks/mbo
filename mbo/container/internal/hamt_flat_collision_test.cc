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
  constexpr bool operator()(int lhs, std::int64_t rhs) const noexcept { return lhs == rhs; }
};

struct HamtFlatCollisionTest : ::testing::Test {};

struct MoveOnlyIdentity final {
  MoveOnlyIdentity() = default;
  MoveOnlyIdentity(const MoveOnlyIdentity&) = delete;
  MoveOnlyIdentity& operator=(const MoveOnlyIdentity&) = delete;
  MoveOnlyIdentity(MoveOnlyIdentity&&) noexcept = default;
  MoveOnlyIdentity& operator=(MoveOnlyIdentity&&) noexcept = default;
  ~MoveOnlyIdentity() = default;

  constexpr int operator()(int value) const noexcept { return value; }
};

TEST_F(HamtFlatCollisionTest, LookupDoesNotCopyKeyExtractionState) {
  HamtFlatCollisionBucket<int, MoveOnlyIdentity, Equal> bucket;
  ASSERT_THAT(bucket.try_insert(7, 11).entry, NotNull());
  EXPECT_THAT(bucket.find(7, std::int64_t{11})->value, Eq(11));
  const auto& const_bucket = bucket;
  EXPECT_THAT(const_bucket.find(7, std::int64_t{11})->value, Eq(11));
}

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
  EXPECT_THAT(bucket.find(7, std::int64_t{13})->value, Eq(13));
  EXPECT_THAT(bucket.erase(7, std::int64_t{11}), Eq(true));
  EXPECT_THAT(bucket.erase(7, std::int64_t{11}), Eq(false));
  EXPECT_THAT(bucket.size(), Eq(1));
  EXPECT_THAT(bucket.begin()->value, Eq(13));
}

TEST_F(HamtFlatCollisionTest, ConstLookupChecksFullHashAndLastErasureEmptiesBucket) {
  HamtFlatCollisionBucket<int, Identity, Equal> bucket;
  ASSERT_THAT(bucket.try_insert(7, 11).entry, NotNull());
  const auto& const_bucket = bucket;
  EXPECT_THAT(const_bucket.find(7, std::int64_t{11})->value, Eq(11));
  EXPECT_THAT(const_bucket.find(8, std::int64_t{11}), Eq(const_bucket.end()));
  EXPECT_THAT(bucket.erase(7, std::int64_t{11}), Eq(true));
  EXPECT_THAT(bucket.begin(), Eq(bucket.end()));
  EXPECT_THAT(bucket.empty(), Eq(true));
}

TEST_F(HamtFlatCollisionTest, ReportsMaximumSizeWithoutMutation) {
  constexpr HamtOptions kOneEntry{.maximum_size = 1};
  HamtFlatCollisionBucket<int, Identity, Equal, kOneEntry> bucket;
  ASSERT_THAT(bucket.try_insert(1, 1).entry, NotNull());

  const auto duplicate = bucket.try_insert(1, 1);
  EXPECT_THAT(duplicate.inserted, Eq(false));
  EXPECT_THAT(duplicate.error, Eq(std::nullopt));

  const auto exhausted = bucket.try_insert(1, 2);

  EXPECT_THAT(exhausted.error, Eq(HamtError::kMaxSizeExceeded));
  EXPECT_THAT(bucket.size(), Eq(1));
}

TEST_F(HamtFlatCollisionTest, StorageExhaustionPreservesExistingEntriesAndAllowsReuse) {
  constexpr SegmentedSequenceOptions kStorageOptions{
      .segment_capacities = {1}, .repeat_last = false, .maximum_size = 1};
  using Entry = HamtStoredValue<std::uint64_t, int>;
  using Storage = SegmentedSequence<Entry, kStorageOptions>;
  HamtFlatCollisionBucket<int, Identity, Equal, HamtOptions{}, std::uint64_t, Entry, Storage> bucket;
  ASSERT_THAT(bucket.try_insert(7, 11).entry, NotNull());
  const auto exhausted = bucket.try_insert(7, 13);
  EXPECT_THAT(exhausted.error, Eq(HamtError::kAllocationExhausted));
  EXPECT_THAT(exhausted.entry, Eq(nullptr));
  EXPECT_THAT(bucket.find(7, std::int64_t{11})->value, Eq(11));
  EXPECT_THAT(bucket.erase(7, std::int64_t{11}), Eq(true));
  ASSERT_THAT(bucket.try_insert(7, 13).entry, NotNull());
  EXPECT_THAT(bucket.begin()->value, Eq(13));
}

}  // namespace
}  // namespace mbo::container::container_internal
