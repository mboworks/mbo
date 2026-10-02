// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/experimental/hamt/hamt_node_string_index.h"
#include "mbo/container/experimental/hamt/hamt_string_index.h"
#include "mbo/strings/experimental/container_string_index.h"

namespace mbo::strings::experimental {
namespace {

using ::testing::Eq;
using ::testing::Optional;

struct StringIndexesPublicApiTest : ::testing::Test {
  template<typename Index>
  void CheckPublicIndexApi() {
    Index index;
    EXPECT_THAT(index.find("value").has_value(), Eq(false));
    EXPECT_THAT(index.try_insert("value", StringId<>(3)), Optional(Eq(true)));
    EXPECT_THAT(index.try_insert("value", StringId<>(4)), Optional(Eq(false)));
    EXPECT_THAT(index.find("value"), Optional(Eq(StringId<>(3))));
  }
};

TEST_F(StringIndexesPublicApiTest, StandardContainerAdapterIsUsableOutsideItsDefiningPackage) {
  CheckPublicIndexApi<ContainerStringIndex<>>();
}

TEST_F(StringIndexesPublicApiTest, FlatHamtAdapterIsUsableOutsideItsDefiningPackage) {
  CheckPublicIndexApi<mbo::container::experimental::hamt::HamtStringIndex<>>();
}

TEST_F(StringIndexesPublicApiTest, NodeHamtAdapterIsUsableOutsideItsDefiningPackage) {
  CheckPublicIndexApi<mbo::container::experimental::hamt::HamtNodeStringIndex<>>();
}

}  // namespace
}  // namespace mbo::strings::experimental
