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
  constexpr bool operator()(int lhs, std::int64_t rhs) const noexcept { return lhs == rhs; }
};

struct HamtCollisionTest : ::testing::Test {};

TEST_F(HamtCollisionTest, DistinguishesFullHashesAndUnequalKeys) {
  constexpr auto kEntries = std::to_array<Entry>({
      Entry{.hash = 17, .key = 1},
      Entry{.hash = 17, .key = 2},
      Entry{.hash = 29, .key = 2},
  });

  EXPECT_THAT(
      FindHamtCollision(kEntries.begin(), kEntries.end(), 17U, std::int64_t{2}, HashOf{}, KeyOf{}, Equal{}),
      Eq(kEntries.begin() + 1));
  EXPECT_THAT(
      FindHamtCollision(kEntries.begin(), kEntries.end(), 29U, std::int64_t{2}, HashOf{}, KeyOf{}, Equal{}),
      Eq(kEntries.begin() + 2));
  EXPECT_THAT(
      FindHamtCollision(kEntries.begin(), kEntries.end(), 17U, std::int64_t{3}, HashOf{}, KeyOf{}, Equal{}),
      Eq(kEntries.end()));
}

constexpr bool IsConstexprUsable() {
  constexpr auto kEntries = std::to_array<Entry>({Entry{.hash = 5, .key = 7}, Entry{.hash = 5, .key = 9}});
  return FindHamtCollision(kEntries.begin(), kEntries.end(), 5U, std::int64_t{9}, HashOf{}, KeyOf{}, Equal{})
         == kEntries.begin() + 1;
}

static_assert(IsConstexprUsable());

}  // namespace
}  // namespace mbo::container::container_internal
