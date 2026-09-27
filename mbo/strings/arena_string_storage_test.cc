// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/arena_string_storage.h"

#include <memory>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::strings {
namespace {
using ::testing::Eq;

struct ArenaStringStorageTest : ::testing::Test {};

TEST_F(ArenaStringStorageTest, ArenaFactoryConstructsConfiguredStorageExactlyOnce) {
  using SmallArena = mbo::memory::Arena<
      mbo::memory::NewDeleteBlockSource,
      mbo::memory::ArenaOptions{
          .initial_block_size = 256, .maximum_block_size = 1'024, .growth_numerator = 1, .growth_denominator = 1}>;
  int calls = 0;
  ArenaStringStorage<SmallArena> storage([&calls]() noexcept {
    ++calls;
    return SmallArena();
  });
  EXPECT_THAT(calls, Eq(1));
  EXPECT_THAT(storage.try_store("configured").value_or(std::string_view{}), Eq("configured"));
  EXPECT_THAT(storage.bytes_reserved(), Eq(256));
  EXPECT_THAT(calls, Eq(1));
}

TEST_F(ArenaStringStorageTest, CopiesBytesIncludingEmbeddedNulsAndDoesNotBorrowInput) {
  ArenaStringStorage<> storage;
  std::string input("a\0b", 3);
  auto stored = storage.try_store(input);
  ASSERT_THAT(stored.has_value(), Eq(true));
  input.assign("changed");
  EXPECT_THAT(stored.value_or(std::string_view{}), Eq(std::string_view("a\0b", 3)));
  EXPECT_THAT(storage.bytes_used(), Eq(3));
}

TEST_F(ArenaStringStorageTest, RewindsUncommittedCopiesWithoutChangingPublishedViews) {
  ArenaStringStorage<> storage;
  const auto published = storage.try_store("published").value_or(std::string_view{});
  const auto checkpoint = storage.checkpoint();
  const auto used = storage.bytes_used();
  const auto temporary = storage.try_store("temporary").value_or(std::string_view{});
  storage.rewind(checkpoint);
  EXPECT_THAT(storage.bytes_used(), Eq(used));
  EXPECT_THAT(published, Eq("published"));
  const auto replacement = storage.try_store("replacement").value_or(std::string_view{});
  EXPECT_THAT(std::addressof(replacement.front()), Eq(std::addressof(temporary.front())));
}

TEST_F(ArenaStringStorageTest, EmptyStringsNeedNoAllocationAndBoundedExhaustionIsSeparate) {
  constexpr mbo::memory::ArenaOptions kOneByteArenaOptions{
      .initial_block_size = 256,
      .maximum_block_size = 256,
      .growth_numerator = 1,
      .growth_denominator = 1,
  };
  using Arena = mbo::memory::Arena<mbo::memory::InlineBlockSource<1>, kOneByteArenaOptions>;
  ArenaStringStorage<Arena> storage;
  EXPECT_THAT(storage.try_store("").has_value(), Eq(true));
  EXPECT_THAT(storage.bytes_reserved(), Eq(0));
  EXPECT_THAT(storage.try_store("x").has_value(), Eq(false));
  EXPECT_THAT(storage.bytes_used(), Eq(0));
}
}  // namespace
}  // namespace mbo::strings
