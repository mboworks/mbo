// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/hamt_options.h"

#include <cstddef>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container {
namespace {

using ::testing::Eq;

struct HamtOptionsTest : ::testing::Test {};

TEST_F(HamtOptionsTest, AcceptsOnlyMeasuredCandidateFragmentWidths) {
  HamtOptions options;
  EXPECT_THAT(options.IsValid(), Eq(true));

  for (std::size_t bits = 4; bits <= 7; ++bits) {
    options.fragment_bits = bits;
    EXPECT_THAT(options.IsValid(), Eq(true));
  }
  options.fragment_bits = 3;
  EXPECT_THAT(options.IsValid(), Eq(false));
  options.fragment_bits = 8;
  EXPECT_THAT(options.IsValid(), Eq(false));
}

TEST_F(HamtOptionsTest, RejectsAZeroMaximumSize) {
  HamtOptions options;
  options.maximum_size = 0;
  EXPECT_THAT(options.IsValid(), Eq(false));
}

static_assert(ValidHamtOptions<HamtOptions{}>);
static_assert(!ValidHamtOptions<HamtOptions{.fragment_bits = 3}>);

}  // namespace
}  // namespace mbo::container
