// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/hamt_string_index.h"

#include <cstddef>
#include <span>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::strings {
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
    EXPECT_THAT(index.try_insert("a", StringId<>(0)), Optional(true));
    const auto snapshot = index;
    index = HamtStringIndex<>();
    EXPECT_THAT(snapshot.find("a"), Optional(StringId<>(0)));
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

struct CountingHash final {
  std::size_t operator()(std::string_view key) const noexcept {
    ++*calls;
    return std::hash<std::string_view>{}(key);
  }

  std::size_t* calls;
};

TEST_F(HamtStringIndexTest, StatefulHashIsRetainedAcrossIndexMutation) {
  std::size_t calls = 0;
  HamtStringIndex<StringId<>, CountingHash> index(CountingHash{.calls = &calls});
  EXPECT_THAT(index.try_insert("stateful", StringId<>(0)), Optional(true));
  const auto before = calls;
  EXPECT_THAT(index.find("stateful"), Optional(StringId<>(0)));
  EXPECT_THAT(calls > before, Eq(true));
}

TEST_F(HamtStringIndexTest, FactoryConstructsNonDefaultSourceWithRecoverableNodeFailure) {
  std::byte buffer{};
  using Index =
      HamtStringIndex<StringId<>, std::hash<std::string_view>, std::equal_to<>, {}, mbo::memory::FixedBlockSource>;
  auto index = Index::try_create(std::hash<std::string_view>{}, std::equal_to<>{}, std::span<std::byte>(&buffer, 1));
  ASSERT_THAT(index.has_value(), Eq(true));
  auto& value = index.value();  // NOLINT(bugprone-unchecked-optional-access): guarded by ASSERT_THAT above.
  EXPECT_THAT(value.try_insert("too-large", StringId<>(0)).has_value(), Eq(false));
  EXPECT_THAT(value.find("too-large").has_value(), Eq(false));
}

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
