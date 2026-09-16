// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_key_of.h"

#include <memory>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container::container_internal {
namespace {
using ::testing::Eq;

struct HamtKeyOfTest : ::testing::Test {};

TEST_F(HamtKeyOfTest, IdentityBorrowsTheOriginalKey) {
  constexpr int kKey = 19;
  constexpr HamtIdentityKey<int> kKeyOf;
  static_assert(kKeyOf(kKey) == 19);
  EXPECT_THAT(std::addressof(kKeyOf(kKey)), Eq(std::addressof(kKey)));
}

TEST_F(HamtKeyOfTest, PairExtractorBorrowsTheImmutableKeyRatherThanTheMappedValue) {
  constexpr std::pair<const int, int> kEntry(19, 29);
  constexpr HamtPairKey<int, int> kKeyOf;
  static_assert(kKeyOf(kEntry) == 19);
  EXPECT_THAT(std::addressof(kKeyOf(kEntry)), Eq(std::addressof(kEntry.first)));
}
}  // namespace
}  // namespace mbo::container::container_internal
