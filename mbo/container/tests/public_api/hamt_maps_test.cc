// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/hamt_flat_map.h"
#include "mbo/container/hamt_node_map.h"

namespace mbo::container {
namespace {

using ::testing::Eq;

struct HamtPublicMapsTest : ::testing::Test {
  template<typename Map>
  void CheckPublicPersistentAndTransientApi() {
    Map original;
    auto edited = original.transient();
    const auto [position, inserted] = edited.insert(typename Map::value_type(42, 7));
    EXPECT_THAT(inserted, Eq(true));
    EXPECT_THAT(position->first, Eq(42));
    EXPECT_THAT(position->second, Eq(7));
    EXPECT_THAT(edited.insert(typename Map::value_type(42, 9)).second, Eq(false));
    EXPECT_THAT(edited.contains(42), Eq(true));
    EXPECT_THAT(edited.at(42), Eq(7));
    EXPECT_THAT(edited.structural_diagnostics().entries, Eq(1));
    EXPECT_THAT(original.empty(), Eq(true));
    const auto snapshot = std::move(edited).persistent();
    EXPECT_THAT(snapshot.contains(42), Eq(true));
    EXPECT_THAT(snapshot.at(42), Eq(7));
    EXPECT_THAT(snapshot.structural_diagnostics().entries, Eq(1));
  }
};

TEST_F(HamtPublicMapsTest, FlatMapIsUsableOutsideItsDefiningPackage) {
  CheckPublicPersistentAndTransientApi<HamtFlatMap<int, int>>();
}

TEST_F(HamtPublicMapsTest, NodeMapIsUsableOutsideItsDefiningPackage) {
  CheckPublicPersistentAndTransientApi<HamtNodeMap<int, int>>();
}

}  // namespace
}  // namespace mbo::container
