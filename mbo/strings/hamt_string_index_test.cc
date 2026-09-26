// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/hamt_string_index.h"

#include <cstddef>
#include <optional>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

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
  EXPECT_THAT(index.try_insert("a", StringId<>(0)), Optional(Eq(true)));
  EXPECT_THAT(index.try_insert(std::string_view("a\0b", 3), StringId<>(1)), Optional(Eq(true)));
  EXPECT_THAT(index.try_insert("a", StringId<>(2)), Optional(Eq(false)));
  EXPECT_THAT(index.find("a"), Optional(Eq(StringId<>(0))));
  EXPECT_THAT(index.find(std::string_view("a\0b", 3)), Optional(Eq(StringId<>(1))));
  EXPECT_THAT(index.find("b"), Eq(std::nullopt));
}

TEST_F(HamtStringIndexTest, ExhaustionDoesNotCommitAnIndexEntry) {
  HamtStringIndex<
      StringId<>, std::hash<std::string_view>, std::equal_to<>, mbo::container::HamtOptions{.maximum_size = 1}>
      index;
  EXPECT_THAT(index.try_insert("a", StringId<>(0)), Optional(Eq(true)));
  EXPECT_THAT(index.try_insert("b", StringId<>(1)), Eq(std::nullopt));
  EXPECT_THAT(index.find("b"), Eq(std::nullopt));
  EXPECT_THAT(index.find("a"), Optional(Eq(StringId<>(0))));
  EXPECT_THAT(index.try_insert("a", StringId<>(2)), Optional(Eq(false)));
  EXPECT_THAT(index.find("a"), Optional(Eq(StringId<>(0))));
}

TEST_F(HamtStringIndexTest, EmptyKeyCanBeInsertedAndLookedUp) {
  HamtStringIndex<> index;
  EXPECT_THAT(index.find(""), Eq(std::nullopt));
  EXPECT_THAT(index.try_insert("", StringId<>(0)), Optional(Eq(true)));
  EXPECT_THAT(index.find(""), Optional(Eq(StringId<>(0))));
  EXPECT_THAT(index.try_insert("", StringId<>(1)), Optional(Eq(false)));
  EXPECT_THAT(index.find(""), Optional(Eq(StringId<>(0))));
}

TEST_F(HamtStringIndexTest, BlockExhaustionPreservesExistingIdsAndDuplicateLookup) {
  using Source = mbo::memory::InlineBlockSource<1'024>;
  HamtStringIndex<StringId<>, CollisionHash, std::equal_to<>, mbo::container::HamtOptions{}, Source> index;
  ASSERT_THAT(index.try_insert("a", StringId<>(0)), Optional(Eq(true)));
  // The source lends one block. Path copying needs a second live block, even
  // though the HAMT entry limit has not been reached.
  EXPECT_THAT(index.try_insert("b", StringId<>(1)), Eq(std::nullopt));
  EXPECT_THAT(index.find("b"), Eq(std::nullopt));
  EXPECT_THAT(index.find("a"), Optional(Eq(StringId<>(0))));
  EXPECT_THAT(index.try_insert("a", StringId<>(2)), Optional(Eq(false)));
  EXPECT_THAT(index.find("a"), Optional(Eq(StringId<>(0))));
}
}  // namespace
}  // namespace mbo::strings
