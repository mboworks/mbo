// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/experimental/hamt/hamt_string_index.h"

#include <cstddef>
#include <optional>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::experimental::hamt {
namespace {
using ::testing::Eq;
using ::testing::Optional;

struct HamtStringIndexTest : ::testing::Test {};

TEST_F(HamtStringIndexTest, CallerOwnedControlStorageRetainsIndexUntilLastSnapshot) {
  mbo::memory::InlineBlockSource<4'096> storage;
  {
    auto created = HamtStringIndex<>::try_create_in(storage, std::hash<std::string_view>{}, std::equal_to<>{});
    if (!created) {
      FAIL() << "control-storage domain creation failed";
      return;
    }
    auto index = std::move(*created);
    created.reset();
    EXPECT_THAT(index.try_insert("a", mbo::strings::StringId<>(0)), Optional(true));
    const auto snapshot = index;
    index = HamtStringIndex<>();
    EXPECT_THAT(snapshot.find("a"), Optional(mbo::strings::StringId<>(0)));
    EXPECT_THAT(
        HamtStringIndex<>::try_create_in(storage, std::hash<std::string_view>{}, std::equal_to<>{}).has_value(),
        Eq(false));
  }
  EXPECT_THAT(
      HamtStringIndex<>::try_create_in(storage, std::hash<std::string_view>{}, std::equal_to<>{}).has_value(),
      Eq(true));
}

struct CollisionHash final {
  std::size_t operator()(std::string_view /*text*/) const noexcept { return 7; }
};

TEST_F(HamtStringIndexTest, DiagnosticsReflectOnlyTheInspectedIndexSnapshot) {
  HamtStringIndex<mbo::strings::StringId<>, CollisionHash> index;
  EXPECT_THAT(index.structural_diagnostics().entries, Eq(0));
  EXPECT_THAT(index.try_insert("a", mbo::strings::StringId<>(0)), Optional(true));
  const auto snapshot = index;
  EXPECT_THAT(index.try_insert("b", mbo::strings::StringId<>(1)), Optional(true));
  const auto measured = index.structural_diagnostics();
  EXPECT_THAT(measured.entries, Eq(2));
  EXPECT_THAT(measured.collision_nodes, Eq(1));
  EXPECT_THAT(measured.collision_entries, Eq(2));
  EXPECT_THAT(measured.largest_collision, Eq(2));
  EXPECT_THAT(snapshot.structural_diagnostics().entries, Eq(1));
  EXPECT_THAT(snapshot.structural_diagnostics().collision_nodes, Eq(0));
  EXPECT_THAT(index.find("b"), Optional(mbo::strings::StringId<>(1)));
}

struct CountingHash final {
  std::size_t operator()(std::string_view key) const noexcept {
    ++*calls;
    return std::hash<std::string_view>{}(key);
  }

  std::size_t* calls;
};

TEST_F(HamtStringIndexTest, StatefulHashIsRetainedAcrossIndexMutation) {
  std::size_t calls = 0;
  HamtStringIndex<mbo::strings::StringId<>, CountingHash> index(CountingHash{.calls = &calls});
  EXPECT_THAT(index.try_insert("stateful", mbo::strings::StringId<>(0)), Optional(true));
  const auto before = calls;
  EXPECT_THAT(index.find("stateful"), Optional(mbo::strings::StringId<>(0)));
  EXPECT_THAT(calls > before, Eq(true));
}

TEST_F(HamtStringIndexTest, FactoryConstructsNonDefaultSourceWithRecoverableNodeFailure) {
  std::byte buffer{};
  using Index = HamtStringIndex<
      mbo::strings::StringId<>, std::hash<std::string_view>, std::equal_to<>, {}, mbo::memory::FixedBlockSource>;
  auto index = Index::try_create(std::hash<std::string_view>{}, std::equal_to<>{}, std::span<std::byte>(&buffer, 1));
  ASSERT_THAT(index.has_value(), Eq(true));
  auto& value = index.value();  // NOLINT(bugprone-unchecked-optional-access): guarded by ASSERT_THAT above.
  EXPECT_THAT(value.try_insert("too-large", mbo::strings::StringId<>(0)).has_value(), Eq(false));
  EXPECT_THAT(value.find("too-large").has_value(), Eq(false));
}

TEST_F(HamtStringIndexTest, FullHashCollisionsAndEmbeddedNulsRetainDistinctDenseIds) {
  HamtStringIndex<mbo::strings::StringId<>, CollisionHash> index;
  EXPECT_THAT(index.try_insert("a", mbo::strings::StringId<>(0)), Optional(Eq(true)));
  EXPECT_THAT(index.try_insert(std::string_view("a\0b", 3), mbo::strings::StringId<>(1)), Optional(Eq(true)));
  EXPECT_THAT(index.try_insert("a", mbo::strings::StringId<>(2)), Optional(Eq(false)));
  EXPECT_THAT(index.find("a"), Optional(Eq(mbo::strings::StringId<>(0))));
  EXPECT_THAT(index.find(std::string_view("a\0b", 3)), Optional(Eq(mbo::strings::StringId<>(1))));
  EXPECT_THAT(index.find("b"), Eq(std::nullopt));
}

TEST_F(HamtStringIndexTest, ExhaustionDoesNotCommitAnIndexEntry) {
  HamtStringIndex<
      mbo::strings::StringId<>, std::hash<std::string_view>, std::equal_to<>,
      mbo::container::experimental::hamt::HamtOptions{.maximum_size = 1}>
      index;
  EXPECT_THAT(index.try_insert("a", mbo::strings::StringId<>(0)), Optional(Eq(true)));
  EXPECT_THAT(index.try_insert("b", mbo::strings::StringId<>(1)), Eq(std::nullopt));
  EXPECT_THAT(index.find("b"), Eq(std::nullopt));
  EXPECT_THAT(index.find("a"), Optional(Eq(mbo::strings::StringId<>(0))));
  EXPECT_THAT(index.try_insert("a", mbo::strings::StringId<>(2)), Optional(Eq(false)));
  EXPECT_THAT(index.find("a"), Optional(Eq(mbo::strings::StringId<>(0))));
}

TEST_F(HamtStringIndexTest, EmptyKeyCanBeInsertedAndLookedUp) {
  HamtStringIndex<> index;
  EXPECT_THAT(index.find(""), Eq(std::nullopt));
  EXPECT_THAT(index.try_insert("", mbo::strings::StringId<>(0)), Optional(Eq(true)));
  EXPECT_THAT(index.find(""), Optional(Eq(mbo::strings::StringId<>(0))));
  EXPECT_THAT(index.try_insert("", mbo::strings::StringId<>(1)), Optional(Eq(false)));
  EXPECT_THAT(index.find(""), Optional(Eq(mbo::strings::StringId<>(0))));
}

TEST_F(HamtStringIndexTest, BlockExhaustionPreservesExistingIdsAndDuplicateLookup) {
  using Source = mbo::memory::InlineBlockSource<1'024>;
  HamtStringIndex<
      mbo::strings::StringId<>, CollisionHash, std::equal_to<>, mbo::container::experimental::hamt::HamtOptions{},
      Source>
      index;
  ASSERT_THAT(index.try_insert("a", mbo::strings::StringId<>(0)), Optional(Eq(true)));
  // The source lends one block. Path copying needs a second live block, even
  // though the HAMT entry limit has not been reached.
  EXPECT_THAT(index.try_insert("b", mbo::strings::StringId<>(1)), Eq(std::nullopt));
  EXPECT_THAT(index.find("b"), Eq(std::nullopt));
  EXPECT_THAT(index.find("a"), Optional(Eq(mbo::strings::StringId<>(0))));
  EXPECT_THAT(index.try_insert("a", mbo::strings::StringId<>(2)), Optional(Eq(false)));
  EXPECT_THAT(index.find("a"), Optional(Eq(mbo::strings::StringId<>(0))));
}
}  // namespace
}  // namespace mbo::container::experimental::hamt
