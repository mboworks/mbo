// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/segmented_options.h"

#include <array>
#include <cstddef>
#include <limits>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct SegmentedOptionsTest : ::testing::Test {};
struct Incomplete;

static_assert(SegmentedElement<int>);
static_assert(!SegmentedElement<const int>);
static_assert(!SegmentedElement<volatile int>);
static_assert(!SegmentedElement<std::remove_const_t<decltype("x")>>);
static_assert(!SegmentedElement<void>);
static_assert(!SegmentedElement<Incomplete>);
static_assert(ValidSegmentedOptions<SegmentedOptions{}>);
static_assert(RepresentableSegmentedOptions<int, SegmentedOptions{}>);
static_assert(!RepresentableSegmentedOptions<int, SegmentedOptions{.segment_size = 0}>);
static_assert(!RepresentableSegmentedOptions<int, SegmentedOptions{.segment_size = 3}>);

TEST_F(SegmentedOptionsTest, DefaultsAndFiniteReservationAreShared) {
  const SegmentedOptions options;
  EXPECT_THAT(options.segment_size, Eq(256));
  EXPECT_THAT(options.segment_capacity, Eq(std::numeric_limits<std::size_t>::max()));
  EXPECT_THAT(options.segment_reservation, Eq(1));
  EXPECT_THAT(options.IsValid(), IsTrue());
  EXPECT_THAT(
      (SegmentedOptions{.segment_size = 1, .segment_capacity = 8, .segment_reservation = 0}.IsValid()), IsTrue());
  EXPECT_THAT(
      (SegmentedOptions{.segment_size = 1, .segment_capacity = 8, .segment_reservation = 8}.IsValid()), IsTrue());
}

TEST_F(SegmentedOptionsTest, RejectsInvalidBounds) {
  constexpr auto kInvalid = std::to_array<SegmentedOptions>({
      {.segment_size = 0},
      {.segment_size = 3},
      {.segment_capacity = 0},
      {.segment_capacity = 3},
      {.segment_reservation = 3},
      {.segment_capacity = 2, .segment_reservation = 4},
  });
  for (const auto& options : kInvalid) {
    EXPECT_THAT(options.IsValid(), IsFalse());
  }
}

}  // namespace
}  // namespace mbo::container
