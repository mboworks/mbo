// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_merge_path.h"

#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::Eq;

struct HamtMergePathTest : ::testing::Test {};

TEST_F(HamtMergePathTest, FindsImmediateAndDeepDivergence) {
  const auto immediate = FindHamtMergePath<std::uint64_t, 5>(0, 1);
  EXPECT_THAT(immediate.common_levels, Eq(0));
  EXPECT_THAT(immediate.existing_fragment, Eq(0));
  EXPECT_THAT(immediate.inserted_fragment, Eq(1));
  EXPECT_THAT(immediate.full_hash_collision, Eq(false));

  const auto deep = FindHamtMergePath<std::uint64_t, 5>(3, 3 + (std::uint64_t{7} << 15));
  EXPECT_THAT(deep.common_levels, Eq(3));
  EXPECT_THAT(deep.existing_fragment, Eq(0));
  EXPECT_THAT(deep.inserted_fragment, Eq(7));
}

TEST_F(HamtMergePathTest, IdentifiesExhaustedFullHashCollisions) {
  const auto collision = FindHamtMergePath<std::uint32_t, 7>(0xdeadbeefU, 0xdeadbeefU, 2);
  EXPECT_THAT(collision.common_levels, Eq(3));
  EXPECT_THAT(collision.full_hash_collision, Eq(true));
}

constexpr auto kConstexprPath = FindHamtMergePath<std::uint32_t, 4>(0x12345678U, 0x123456f8U);
static_assert(kConstexprPath.common_levels == 1);
static_assert(kConstexprPath.existing_fragment == 7);
static_assert(kConstexprPath.inserted_fragment == 15);

}  // namespace
}  // namespace mbo::container::container_internal
