// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/hamt_string_index.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::strings {
namespace {
using ::testing::Eq;
using ::testing::Optional;

struct HamtStringIndexTest : ::testing::Test {};

struct CollisionHash final {
  std::size_t operator()(std::string_view /*text*/) const noexcept { return 7; }
};

TEST_F(HamtStringIndexTest, FullHashCollisionsAndEmbeddedNulsRetainDistinctDenseIds) {
  HamtStringIndex<StringId<>, CollisionHash> index;
  EXPECT_THAT(index.try_insert("a", StringId<>(0)), Optional(true));
  EXPECT_THAT(index.try_insert(std::string_view("a\0b", 3), StringId<>(1)), Optional(true));
  EXPECT_THAT(index.try_insert("a", StringId<>(2)), Optional(false));
  EXPECT_THAT(index.find("a"), Optional(StringId<>(0)));
  EXPECT_THAT(index.find(std::string_view("a\0b", 3)), Optional(StringId<>(1)));
  EXPECT_THAT(index.find("b").has_value(), Eq(false));
}

TEST_F(HamtStringIndexTest, ExhaustionDoesNotCommitAnIndexEntry) {
  HamtStringIndex<
      StringId<>, std::hash<std::string_view>, std::equal_to<>, mbo::container::HamtOptions{.maximum_size = 1}>
      index;
  EXPECT_THAT(index.try_insert("a", StringId<>(0)), Optional(true));
  EXPECT_THAT(index.try_insert("b", StringId<>(1)).has_value(), Eq(false));
  EXPECT_THAT(index.find("b").has_value(), Eq(false));
  EXPECT_THAT(index.find("a"), Optional(StringId<>(0)));
}
}  // namespace
}  // namespace mbo::strings
