// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_bitmap.h"

#include <array>
#include <cstddef>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::Eq;

struct HamtBitmapTest : ::testing::Test {
  template<std::size_t Bits>
  static void ExerciseWidth() {
    HamtBitmap<Bits> bitmap;
    std::array<bool, HamtBitmap<Bits>::kSlotCount> occupied{};

    for (std::size_t slot = 0; slot < occupied.size(); slot += 3) {
      EXPECT_THAT(bitmap.set(slot), Eq(true));
      EXPECT_THAT(bitmap.set(slot), Eq(false));
      occupied[slot] = true;
    }

    std::size_t expected_rank = 0;
    for (std::size_t slot = 0; slot < occupied.size(); ++slot) {
      EXPECT_THAT(bitmap.contains(slot), Eq(occupied[slot]));
      EXPECT_THAT(bitmap.rank(slot), Eq(expected_rank));
      expected_rank += occupied[slot] ? 1 : 0;
    }
    EXPECT_THAT(bitmap.size(), Eq(expected_rank));

    for (std::size_t slot = 0; slot < occupied.size(); ++slot) {
      EXPECT_THAT(bitmap.reset(slot), Eq(occupied[slot]));
    }
    EXPECT_THAT(bitmap.size(), Eq(0));
  }
};

TEST_F(HamtBitmapTest, SupportsEveryCandidateFragmentWidth) {
  ExerciseWidth<4>();
  ExerciseWidth<5>();
  ExerciseWidth<6>();
  ExerciseWidth<7>();
}

constexpr bool IsConstexprUsable() {
  HamtBitmap<7> bitmap;
  return bitmap.set(0) && bitmap.set(64) && bitmap.set(127) && bitmap.rank(127) == 2 && bitmap.size() == 3
         && bitmap.reset(64) && !bitmap.contains(64);
}

static_assert(IsConstexprUsable());

}  // namespace
}  // namespace mbo::container::container_internal
