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

    EXPECT_THAT(index.insert_data(1), Eq(true));
    EXPECT_THAT(index.insert_data(last), Eq(true));
    EXPECT_THAT(index.insert_node(2), Eq(true));
    EXPECT_THAT(index.insert_node(last), Eq(false));
    EXPECT_THAT(index.insert_data(2), Eq(false));
    EXPECT_THAT(index.data_index(last), Eq(1));
    EXPECT_THAT(index.node_index(2), Eq(0));
    EXPECT_THAT(index.data_size(), Eq(2));
    EXPECT_THAT(index.node_size(), Eq(1));

    EXPECT_THAT(index.promote_data_to_node(1), Eq(true));
    EXPECT_THAT(index.kind(1), Eq(HamtSlotKind::kNode));
    EXPECT_THAT(index.demote_node_to_data(2), Eq(true));
    EXPECT_THAT(index.kind(2), Eq(HamtSlotKind::kData));
    EXPECT_THAT(index.promote_data_to_node(0), Eq(false));
    EXPECT_THAT(index.demote_node_to_data(0), Eq(false));
    EXPECT_THAT(index.erase_data(last), Eq(true));
    EXPECT_THAT(index.erase_data(last), Eq(false));
    EXPECT_THAT(index.erase_node(1), Eq(true));
    EXPECT_THAT(index.erase_node(1), Eq(false));
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
  return index.insert_data(3) && index.promote_data_to_node(3) && index.node_index(3) == 0
         && index.demote_node_to_data(3) && index.data_size() == 1;
}

static_assert(IsConstexprUsable());

}  // namespace
}  // namespace mbo::container::container_internal
