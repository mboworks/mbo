// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/experimental/hamt/hamt_string_index.h"

#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <string_view>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"
#include "mbo/strings/string_interner_types.h"

namespace mbo::container::experimental::hamt {
namespace {
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

using Id = mbo::strings::StringIdU32<>;
static_assert(mbo::strings::StringInternerIdIndex<HamtStringIndex<>, Id>);
static_assert(!std::is_copy_constructible_v<HamtStringIndex<>>);

struct HamtStringIndexTest : ::testing::Test {};

struct CollisionHash final {
  static std::size_t operator()(std::string_view /*key*/) noexcept { return 7; }
};

TEST_F(HamtStringIndexTest, ContainerInterfacePreservesIdsAcrossCollisionsAndErasure) {
  HamtStringIndex<Id, CollisionHash> index;
  EXPECT_THAT(index, IsEmpty());
  const auto first = index.emplace("a", Id(0));
  ASSERT_THAT(first.second, Eq(true));
  EXPECT_THAT(first.first->second, Eq(Id(0)));
  EXPECT_THAT(index.emplace("a", Id(9)).second, Eq(false));
  const std::string_view embedded("a\0b", 3);
  EXPECT_THAT(index.emplace(embedded, Id(1)).second, Eq(true));
  EXPECT_THAT(index, SizeIs(2));
  EXPECT_THAT(index.find(embedded)->second, Eq(Id(1)));
  EXPECT_THAT(index.find("missing"), Eq(index.end()));
  index.erase(index.find("a"));
  EXPECT_THAT(index, SizeIs(1));
  EXPECT_THAT(index.find("a"), Eq(index.end()));
  EXPECT_THAT(index.find(embedded)->second, Eq(Id(1)));
}

TEST_F(HamtStringIndexTest, EmptyKeysAndMovesRetainContainerSemantics) {
  HamtStringIndex<> index;
  index.emplace("", Id(0));
  auto moved = std::move(index);
  EXPECT_THAT(moved.find("")->second, Eq(Id(0)));
  HamtStringIndex<> assigned;
  assigned = std::move(moved);
  EXPECT_THAT(assigned.begin()->first, Eq(""));
  EXPECT_THAT(assigned, SizeIs(1));
}

TEST_F(HamtStringIndexTest, CallerOwnedControlStorageIsReleasedWithTheIndex) {
  mbo::memory::InlineBlockSource<4'096> storage;
  {
    auto created = HamtStringIndex<>::try_create_in(storage, std::hash<std::string_view>{}, std::equal_to<>{});
    if (!created) {
      FAIL() << "control-storage domain creation failed";
      return;
    }
    created->emplace("a", Id(0));
    EXPECT_THAT(
        HamtStringIndex<>::try_create_in(storage, std::hash<std::string_view>{}, std::equal_to<>{}).has_value(),
        Eq(false));
  }
  EXPECT_THAT(
      HamtStringIndex<>::try_create_in(storage, std::hash<std::string_view>{}, std::equal_to<>{}).has_value(),
      Eq(true));
}

TEST_F(HamtStringIndexTest, FactoriesPropagateControlAndNodeExhaustion) {
  mbo::memory::InlineBlockSource<1> control;
  EXPECT_THAT(
      HamtStringIndex<>::try_create_in(control, std::hash<std::string_view>{}, std::equal_to<>{}).has_value(),
      Eq(false));
  using Index = HamtStringIndex<Id, CollisionHash, std::equal_to<>, {}, mbo::memory::FixedBlockSource>;
  std::array<std::byte, 1> bytes{};
  auto created = Index::try_create(CollisionHash{}, std::equal_to<>{}, std::span<std::byte>(bytes));
  if (!created) {
    FAIL() << "source-domain creation failed";
    return;
  }
  const auto result = created->try_emplace("a", Id(0));
  ASSERT_THAT(result.has_value(), Eq(false));
  EXPECT_THAT(result.error(), Eq(HamtError::kAllocationExhausted));
  EXPECT_THAT(*created, IsEmpty());
}

TEST_F(HamtStringIndexTest, RecoverableSizeAndCollisionLimitsPreserveExistingEntries) {
  HamtStringIndex<Id, CollisionHash, std::equal_to<>, {.maximum_size = 1}> bounded;
  ASSERT_THAT(bounded.try_emplace("a", Id(0)).has_value(), Eq(true));
  const auto full = bounded.try_emplace("b", Id(1));
  ASSERT_THAT(full.has_value(), Eq(false));
  EXPECT_THAT(full.error(), Eq(HamtError::kMaxSizeExceeded));
  ASSERT_THAT(bounded.try_emplace("a", Id(9)).has_value(), Eq(true));
  EXPECT_THAT(bounded.find("a")->second, Eq(Id(0)));

  HamtStringIndex<Id, CollisionHash, std::equal_to<>, {.maximum_collision_size = 1}> collisions;
  collisions.emplace("a", Id(0));
  const auto collision = collisions.try_emplace("b", Id(1));
  ASSERT_THAT(collision.has_value(), Eq(false));
  EXPECT_THAT(collision.error(), Eq(HamtError::kCollisionLimitExceeded));
}

TEST_F(HamtStringIndexTest, StatefulHashAndDiagnosticsRemainAvailable) {
  struct Hash final {
    std::size_t* calls;

    std::size_t operator()(std::string_view text) const noexcept {
      ++*calls;
      return std::hash<std::string_view>{}(text);
    }
  };

  std::size_t calls = 0;
  HamtStringIndex<Id, Hash> index(Hash{.calls = &calls});
  index.emplace("a", Id(0));
  const auto before = calls;
  const auto& read = index;
  EXPECT_THAT(read.find("a")->second, Eq(Id(0)));
  EXPECT_THAT(calls > before, Eq(true));
  EXPECT_THAT(read.structural_diagnostics().entries, Eq(1));
  std::size_t nodes = 0;
  read.visit_node_diagnostics([&nodes](const auto&) noexcept { ++nodes; });
  EXPECT_THAT(nodes > 0, Eq(true));
}

}  // namespace
}  // namespace mbo::container::experimental::hamt
