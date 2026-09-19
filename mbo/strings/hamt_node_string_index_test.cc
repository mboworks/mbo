// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/hamt_node_string_index.h"

#include <cstddef>
#include <exception>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/strings/string_interner.h"

namespace mbo::strings {
namespace {
using ::testing::Eq;
using ::testing::Optional;

struct HamtNodeStringIndexTest : ::testing::Test {};

TEST_F(HamtNodeStringIndexTest, BoundedControlAndNodeStoragePropagateFailureThroughInterner) {
  using Index = HamtNodeStringIndex<
      StringId<>, std::hash<std::string_view>, std::equal_to<>, {}, mbo::memory::InlineBlockSource<1>>;
  using Interner =
      StringInterner<std::uint32_t, ArenaStringStorage<>, mbo::container::SegmentedSequence<std::string_view>, Index>;
  mbo::memory::InlineBlockSource<4'096> control;
  {
    Interner interner(nullptr, [&control]() noexcept {
      auto created = Index::try_create_in(control, std::hash<std::string_view>{}, std::equal_to<>{});
      if (!created) {
        std::terminate();
      }
      return std::move(*created);
    });
    EXPECT_THAT(interner.intern("uncommitted").index(), Eq(1));
    EXPECT_THAT(interner.size(), Eq(0));
    EXPECT_THAT(interner.local_character_bytes_used(), Optional(0));
    EXPECT_THAT(interner.find("uncommitted").has_value(), Eq(false));
    EXPECT_THAT(Index::try_create_in(control, std::hash<std::string_view>{}, std::equal_to<>{}).has_value(), Eq(false));
  }
  EXPECT_THAT(Index::try_create_in(control, std::hash<std::string_view>{}, std::equal_to<>{}).has_value(), Eq(true));
}

struct CollisionHash final {
  std::size_t operator()(std::string_view /*unused*/) const noexcept { return 7; }
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
