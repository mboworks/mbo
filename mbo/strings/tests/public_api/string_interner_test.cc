// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/string_interner.h"

#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/strings/string_interner_map.h"

namespace mbo::strings {
namespace {

using ::testing::Eq;
using ::testing::Optional;

struct StringInternerPublicApiTest : ::testing::Test {};

TEST_F(StringInternerPublicApiTest, CascadingInternerIsUsableOutsideItsDefiningPackage) {
  StringInterner root;
  EXPECT_THAT(root.try_intern_id("root"), Optional(Eq(StringId<>(0))));

  StringInterner child(&root);
  EXPECT_THAT(root.try_intern_id("late"), Optional(Eq(StringId<>(1))));
  EXPECT_THAT(child.find("root"), Optional(Eq(StringId<>(0))));
  EXPECT_THAT(child.find("late").has_value(), Eq(false));
  EXPECT_THAT(child.try_intern_id("child"), Optional(Eq(StringId<>(1))));
  EXPECT_THAT(child.get(StringId<>(1)), Optional(Eq(std::string_view("child"))));
}

TEST_F(StringInternerPublicApiTest, CascadingMapIterationIsUsableOutsideItsDefiningPackage) {
  StringInternerMap<int> root;
  EXPECT_THAT(root.try_emplace("root", 1).index(), Eq(0));
  StringInternerMap<int> child(&root);
  EXPECT_THAT(child.try_emplace("child", 2).index(), Eq(0));
  auto position = child.begin();
  EXPECT_THAT((*position).key, Eq("root"));
  EXPECT_THAT((*position++).mapped, Eq(1));
  EXPECT_THAT((*position).key, Eq("child"));
  EXPECT_THAT((*position++).mapped, Eq(2));
  EXPECT_THAT(position == child.end(), Eq(true));
}

}  // namespace
}  // namespace mbo::strings
