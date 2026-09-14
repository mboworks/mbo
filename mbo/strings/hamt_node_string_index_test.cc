// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/hamt_node_string_index.h"

#include <cstddef>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/strings/string_interner.h"

namespace mbo::strings {
namespace {
using ::testing::Eq;
using ::testing::Optional;

struct HamtNodeStringIndexTest : ::testing::Test {};

struct CollisionHash final {
  std::size_t operator()(std::string_view) const noexcept { return 7; }
};

TEST_F(HamtNodeStringIndexTest, BoundedSizePreservesDuplicateIdsAndExistingSnapshots) {
  HamtNodeStringIndex<StringId<>, CollisionHash, std::equal_to<>, mbo::container::HamtOptions{.maximum_size = 1}> index;
  EXPECT_THAT(index.try_insert("a", StringId<>(0)), Optional(true));
  const auto snapshot = index;
  EXPECT_THAT(index.try_insert("a", StringId<>(1)), Optional(false));
  EXPECT_THAT(index.try_insert("b", StringId<>(1)).has_value(), Eq(false));
  EXPECT_THAT(index.find("a"), Optional(StringId<>(0)));
  EXPECT_THAT(index.find("b").has_value(), Eq(false));
  EXPECT_THAT(snapshot.find("a"), Optional(StringId<>(0)));
}

TEST_F(HamtNodeStringIndexTest, PayloadAllocationFailureDoesNotPublishAnEntry) {
  HamtNodeStringIndex<StringId<>, CollisionHash, std::equal_to<>, {}, mbo::memory::InlineBlockSource<1>> index;
  EXPECT_THAT(index.try_insert("a", StringId<>(0)).has_value(), Eq(false));
  EXPECT_THAT(index.find("a").has_value(), Eq(false));
}

TEST_F(HamtNodeStringIndexTest, CollisionSnapshotsPreserveIdsAndDuplicateSemantics) {
  HamtNodeStringIndex<StringId<>, CollisionHash> index;
  EXPECT_THAT(index.try_insert("a", StringId<>(0)), Optional(true));
  const auto snapshot = index;
  EXPECT_THAT(index.try_insert("b", StringId<>(1)), Optional(true));
  EXPECT_THAT(index.try_insert("a", StringId<>(2)), Optional(false));
  EXPECT_THAT(index.find("a"), Optional(StringId<>(0)));
  EXPECT_THAT(index.find("b"), Optional(StringId<>(1)));
  EXPECT_THAT(snapshot.find("a"), Optional(StringId<>(0)));
  EXPECT_THAT(snapshot.find("b").has_value(), Eq(false));
}

TEST_F(HamtNodeStringIndexTest, InternerUsesTheSameCascadeContractWithNodeStorage) {
  using Interner = StringInterner<
      std::uint32_t, ArenaStringStorage<>, mbo::container::SegmentedSequence<std::string_view>, HamtNodeStringIndex<>>;
  Interner root;
  EXPECT_THAT(root.intern("root").index(), Eq(0));
  Interner child(&root);
  EXPECT_THAT(root.intern("later").index(), Eq(0));
  EXPECT_THAT(child.find("later").has_value(), Eq(false));
  EXPECT_THAT(child.intern("child").index(), Eq(0));
  EXPECT_THAT(child.find("root"), Optional(StringId<>(0)));
  EXPECT_THAT(child.rfind("child"), Optional(StringId<>(1)));
}
}  // namespace
}  // namespace mbo::strings
