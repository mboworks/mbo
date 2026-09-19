// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/segmented_sequence.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;

struct SegmentedSequencePublicApiTest : ::testing::Test {};

TEST_F(SegmentedSequencePublicApiTest, StableSequenceIsUsableOutsideItsDefiningPackage) {
  SegmentedSequence<int> sequence;
  const int* const first = &sequence.push_back(1);
  sequence.push_back(2);
  EXPECT_THAT(&sequence.front(), Eq(first));
  EXPECT_THAT(sequence, ElementsAre(1, 2));
  EXPECT_THAT(sequence.pop_back_value(), Eq(2));
  EXPECT_THAT(sequence, ElementsAre(1));
}

}  // namespace
}  // namespace mbo::container
