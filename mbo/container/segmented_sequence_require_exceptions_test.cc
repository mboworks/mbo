// SPDX-FileCopyrightText: Copyright (c) M. Boerger, The MBO Works Authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/config/config.h"
#include "mbo/container/segmented_sequence.h"
#include "mbo/memory/block_source.h"

namespace mbo::container {
namespace {

using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::ThrowsMessage;

constexpr SegmentedSequenceOptions kTwoSegments{
    .segment_capacities = {2, 3},
    .listed_capacities = 2,
    .repeat_last = false,
    .maximum_size = 5,
};

struct SegmentedSequenceRequireExceptionsTest : ::testing::Test {};

TEST_F(SegmentedSequenceRequireExceptionsTest, ReserveRollsBackNewSegmentsAfterAllocationFailure) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  alignas(int) std::array<std::byte, sizeof(int) * 2> storage{};
  SegmentedSequence<int, kTwoSegments, mbo::memory::FixedBlockSource> sequence(
      mbo::memory::FixedBlockSource(std::span<std::byte>(storage), alignof(int)));

  EXPECT_THAT([&sequence] { sequence.reserve(5); }, ThrowsMessage<std::runtime_error>(HasSubstr("allocation failed")));
  EXPECT_THAT(sequence, IsEmpty());
  EXPECT_THAT(sequence.capacity(), 0);
  EXPECT_THAT(sequence.segment_count(), 0);
}

}  // namespace
}  // namespace mbo::container
