// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/config/config.h"
#include "mbo/container/experimental/frozen_map.h"
#include "mbo/container/experimental/frozen_options.h"
#include "mbo/container/experimental/frozen_set.h"

namespace mbo::container::experimental {
namespace {
using ::testing::HasSubstr;
using ::testing::ThrowsMessage;

struct FrozenRequireExceptionsTest : ::testing::Test {
 protected:
  void SetUp() override {
    if constexpr (!config::kRequireThrows) {
      GTEST_SKIP() << "Requires --//mbo/config:require_throws=true";
    }
  }
};

struct ConstantHash {
  constexpr std::uint64_t operator()(int /*key*/) const { return 0; }
};

struct IdentityHash {
  constexpr std::uint64_t operator()(int key) const { return static_cast<std::uint64_t>(key); }
};

TEST_F(FrozenRequireExceptionsTest, ConflictingDuplicates) {
  EXPECT_THAT(
      ([] { const FrozenMap<int, int, 2> table({{1, 2}, {1, 3}}); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("Frozen conflicting duplicate key")));
}

TEST_F(FrozenRequireExceptionsTest, UncomparableDuplicates) {
  struct Value {
    int value;
  };

  EXPECT_THAT(
      ([] { const FrozenMap<int, Value, 2> table({{1, {2}}, {1, {2}}}); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("Frozen duplicate mapped values cannot be compared")));
}

TEST_F(FrozenRequireExceptionsTest, DistinctKeysWithTheSameHash) {
  EXPECT_THAT(
      ([] { const FrozenSet<int, 2, ConstantHash> table({1, 2}); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("Frozen distinct keys have identical hashes")));
}

TEST_F(FrozenRequireExceptionsTest, BoundedSeedSearch) {
  EXPECT_THAT(
      ([] { const FrozenSet<int, FrozenOptions{.capacity = 2, .max_seed = 0}, IdentityHash> table({0, 2}); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("Frozen perfect hash seed search exhausted")));
}

TEST_F(FrozenRequireExceptionsTest, DifferentHashesCanCollideInTheSameSlot) {
  EXPECT_THAT(
      ([] {
        const FrozenSet<int, FrozenOptions{.capacity = 2, .slots = 2, .max_seed = 1}, IdentityHash> table({2, 4});
      }),
      ThrowsMessage<std::runtime_error>(HasSubstr("Frozen perfect hash seed search exhausted")));
}

TEST_F(FrozenRequireExceptionsTest, WorkAndByteBudgets) {
  EXPECT_THAT(
      ([] { const FrozenSet<int, FrozenOptions{.capacity = 2, .max_work = 0}> table({1}); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("Frozen construction work budget exhausted")));
  EXPECT_THAT(
      ([] { const FrozenSet<std::string_view, FrozenOptions{.capacity = 2, .max_key_bytes = 3}> table({"ab", "cd"}); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("Frozen key byte budget exhausted")));
}

TEST_F(FrozenRequireExceptionsTest, CheckedCapacities) {
  EXPECT_THAT(
      ([] { const FrozenSet<int, 0> table({1}); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("Frozen slot capacity exceeded")));
  EXPECT_THAT(
      ([] { const FrozenSet<int, FrozenOptions{.capacity = 2, .slots = 1}> table({1, 2}); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("Frozen slot capacity exceeded")));
  EXPECT_THAT(
      ([] { const FrozenSet<int, 1> table({1, 2}); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("Frozen element capacity exceeded")));
}

TEST_F(FrozenRequireExceptionsTest, CheckedObservers) {
  const FrozenMap<int, int, 1> table({{1, 2}});
  EXPECT_THAT(
      ([&table] { return table.at(0); }), ThrowsMessage<std::runtime_error>(HasSubstr("FrozenMap key not found")));
  EXPECT_THAT(
      ([&table] { return table.at_index(1); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("Frozen index out of range")));
  EXPECT_THAT(
      ([&table] { return table.bucket_size(table.bucket_count()); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("Frozen bucket out of range")));
  EXPECT_THAT(
      ([&table] { return table.end(table.bucket_count()); }),
      ThrowsMessage<std::runtime_error>(HasSubstr("Frozen bucket out of range")));
}
}  // namespace
}  // namespace mbo::container::experimental
