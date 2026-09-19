// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/string_interner.h"

#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

}  // namespace
}  // namespace mbo::strings
