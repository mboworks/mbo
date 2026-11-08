// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/experimental/hamt/hamt_node_string_index.h"

#include <functional>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/strings/string_interner_types.h"

namespace mbo::container::experimental::hamt {
namespace {
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

using Id = mbo::strings::StringIdU32<>;
static_assert(mbo::strings::StringInternerIdIndex<HamtNodeStringIndex<>, Id>);

struct HamtNodeStringIndexTest : ::testing::Test {};

TEST_F(HamtNodeStringIndexTest, NodeContainerPreservesIdsAndSupportsErasure) {
  HamtNodeStringIndex<> index;
  EXPECT_THAT(index.emplace("a", Id(0)).second, Eq(true));
  EXPECT_THAT(index.emplace("b", Id(1)).second, Eq(true));
  EXPECT_THAT(index.emplace("a", Id(9)).second, Eq(false));
  EXPECT_THAT(index.find("a")->second, Eq(Id(0)));
  index.erase(index.find("a"));
  EXPECT_THAT(index, SizeIs(1));
  EXPECT_THAT(index.find("a"), Eq(index.end()));
  EXPECT_THAT(index.begin()->second, Eq(Id(1)));
}

TEST_F(HamtNodeStringIndexTest, PayloadExhaustionDoesNotPublishAnEntry) {
  using Index =
      HamtNodeStringIndex<Id, std::hash<std::string_view>, std::equal_to<>, {}, mbo::memory::InlineBlockSource<1>>;
  auto created = Index::try_create(std::hash<std::string_view>{}, std::equal_to<>{});
  if (!created) {
    FAIL() << "source-domain creation failed";
    return;
  }
  const auto result = created->try_emplace("a", Id(0));
  ASSERT_THAT(result.has_value(), Eq(false));
  EXPECT_THAT(result.error(), Eq(HamtError::kAllocationExhausted));
  EXPECT_THAT(*created, IsEmpty());
}

}  // namespace
}  // namespace mbo::container::experimental::hamt
