// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_node_index.h"

#include <cstddef>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::Eq;

struct HamtNodeIndexTest : ::testing::Test {
  template<std::size_t Bits>
  static void ExerciseWidth() {
    HamtNodeIndex<Bits> index;
    const std::size_t last = index.kSlotCount - 1;

    EXPECT_THAT(index.InsertData(1), Eq(true));
    EXPECT_THAT(index.InsertData(last), Eq(true));
    EXPECT_THAT(index.InsertNode(2), Eq(true));
    EXPECT_THAT(index.InsertNode(last), Eq(false));
    EXPECT_THAT(index.InsertData(2), Eq(false));
    EXPECT_THAT(index.DataIndex(last), Eq(1));
    EXPECT_THAT(index.NodeIndex(2), Eq(0));
    EXPECT_THAT(index.DataSize(), Eq(2));
    EXPECT_THAT(index.NodeSize(), Eq(1));

    EXPECT_THAT(index.PromoteDataToNode(1), Eq(true));
    EXPECT_THAT(index.Kind(1), Eq(HamtSlotKind::kNode));
    EXPECT_THAT(index.DemoteNodeToData(2), Eq(true));
    EXPECT_THAT(index.Kind(2), Eq(HamtSlotKind::kData));
    EXPECT_THAT(index.PromoteDataToNode(0), Eq(false));
    EXPECT_THAT(index.DemoteNodeToData(0), Eq(false));
    EXPECT_THAT(index.EraseData(last), Eq(true));
    EXPECT_THAT(index.EraseData(last), Eq(false));
    EXPECT_THAT(index.EraseNode(1), Eq(true));
    EXPECT_THAT(index.EraseNode(1), Eq(false));
  }
};

TEST_F(HamtNodeIndexTest, MaintainsDisjointDenseRanksAtEveryCandidateWidth) {
  ExerciseWidth<4>();
  ExerciseWidth<5>();
  ExerciseWidth<6>();
  ExerciseWidth<7>();
}

constexpr bool IsConstexprUsable() {
  HamtNodeIndex<5> index;
  return index.InsertData(3) && index.PromoteDataToNode(3) && index.NodeIndex(3) == 0 && index.DemoteNodeToData(3)
         && index.DataSize() == 1;
}

static_assert(IsConstexprUsable());

}  // namespace
}  // namespace mbo::container::container_internal
