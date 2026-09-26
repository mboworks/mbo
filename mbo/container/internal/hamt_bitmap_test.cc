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
      EXPECT_THAT(bitmap.Set(slot), Eq(true));
      EXPECT_THAT(bitmap.Set(slot), Eq(false));
      occupied.at(slot) = true;
    }

    std::size_t expected_rank = 0;
    for (std::size_t slot = 0; slot < occupied.size(); ++slot) {
      EXPECT_THAT(bitmap.Contains(slot), Eq(occupied.at(slot)));
      EXPECT_THAT(bitmap.Rank(slot), Eq(expected_rank));
      expected_rank += occupied.at(slot) ? 1 : 0;
    }
    EXPECT_THAT(bitmap.Size(), Eq(expected_rank));

    for (std::size_t slot = 0; slot < occupied.size(); ++slot) {
      EXPECT_THAT(bitmap.Reset(slot), Eq(occupied.at(slot)));
    }
    EXPECT_THAT(bitmap.Size(), Eq(0));
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
  return bitmap.Set(0) && bitmap.Set(64) && bitmap.Set(127) && bitmap.Rank(127) == 2 && bitmap.Size() == 3
         && bitmap.Reset(64) && !bitmap.Contains(64);
}

static_assert(IsConstexprUsable());

}  // namespace
}  // namespace mbo::container::container_internal
