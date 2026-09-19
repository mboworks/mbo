// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/hamt_flat_set.h"
#include "mbo/container/hamt_node_set.h"

namespace mbo::container {
namespace {

using ::testing::Eq;

struct HamtPublicSetsTest : ::testing::Test {
  template<typename Set>
  void CheckPublicPersistentAndTransientApi() {
    Set original;
    auto edited = original.transient();
    EXPECT_THAT(edited.insert(42).second, Eq(true));
    EXPECT_THAT(edited.insert(42).second, Eq(false));
    EXPECT_THAT(edited.contains(42), Eq(true));
    EXPECT_THAT(edited.structural_diagnostics().entries, Eq(1));
    EXPECT_THAT(original.empty(), Eq(true));
    const auto snapshot = std::move(edited).persistent();
    EXPECT_THAT(snapshot.contains(42), Eq(true));
    EXPECT_THAT(snapshot.structural_diagnostics().entries, Eq(1));
  }
};

TEST_F(HamtPublicSetsTest, FlatSetIsUsableOutsideItsDefiningPackage) {
  CheckPublicPersistentAndTransientApi<HamtFlatSet<int>>();
}

TEST_F(HamtPublicSetsTest, NodeSetIsUsableOutsideItsDefiningPackage) {
  CheckPublicPersistentAndTransientApi<HamtNodeSet<int>>();
}

}  // namespace
}  // namespace mbo::container
