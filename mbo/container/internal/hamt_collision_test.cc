// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_collision.h"

#include <array>
#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::Eq;

struct Entry final {
  std::uint64_t hash;
  int key;
};

struct HashOf final {
  constexpr std::uint64_t operator()(const Entry& entry) const noexcept { return entry.hash; }
};

struct KeyOf final {
  constexpr int operator()(const Entry& entry) const noexcept { return entry.key; }
};

struct Equal final {
  constexpr bool operator()(int lhs, long rhs) const noexcept { return lhs == rhs; }
};

struct HamtCollisionTest : ::testing::Test {};

TEST_F(HamtCollisionTest, DistinguishesFullHashesAndUnequalKeys) {
  constexpr std::array entries = {
      Entry{.hash = 17, .key = 1},
      Entry{.hash = 17, .key = 2},
      Entry{.hash = 29, .key = 2},
  };

  EXPECT_THAT(
      FindHamtCollision(entries.begin(), entries.end(), 17U, 2L, HashOf{}, KeyOf{}, Equal{}), Eq(entries.begin() + 1));
  EXPECT_THAT(
      FindHamtCollision(entries.begin(), entries.end(), 29U, 2L, HashOf{}, KeyOf{}, Equal{}), Eq(entries.begin() + 2));
  EXPECT_THAT(
      FindHamtCollision(entries.begin(), entries.end(), 17U, 3L, HashOf{}, KeyOf{}, Equal{}), Eq(entries.end()));
}

constexpr bool IsConstexprUsable() {
  constexpr std::array entries = {Entry{.hash = 5, .key = 7}, Entry{.hash = 5, .key = 9}};
  return FindHamtCollision(entries.begin(), entries.end(), 5U, 9L, HashOf{}, KeyOf{}, Equal{}) == entries.begin() + 1;
}

static_assert(IsConstexprUsable());

}  // namespace
}  // namespace mbo::container::container_internal
