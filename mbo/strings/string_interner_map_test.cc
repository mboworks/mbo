// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/string_interner_map.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/limited_vector.h"
#include "mbo/memory/arena_block_source.h"

namespace mbo::strings {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::VariantWith;
using Insertion = std::pair<StringId<>, bool>;

static_assert(
    noexcept(std::declval<StringInternerMap<int>&>().try_emplace(std::string_view{}, 0))
    == !::mbo::config::kRequireThrows);

struct StringInternerMapTest : ::testing::Test {};

static_assert(std::bidirectional_iterator<StringInternerMap<int>::iterator>);
static_assert(noexcept(*std::declval<StringInternerMap<int>::iterator&>()) == !::mbo::config::kRequireThrows);
static_assert(noexcept(++std::declval<StringInternerMap<int>::iterator&>()) == !::mbo::config::kRequireThrows);
static_assert(noexcept(--std::declval<StringInternerMap<int>::iterator&>()) == !::mbo::config::kRequireThrows);

TEST_F(StringInternerMapTest, ProvidesStableMappedAddressesAndDenseStringIds) {
  StringInternerMap<int> map;
  const auto first = map.try_emplace("first", 1);
  const auto* const first_insertion = std::get_if<Insertion>(&first);
  ASSERT_THAT(first_insertion, NotNull());
  const auto first_id = first_insertion->first;
  const int* const first_value = map.mapped(first_id);
  ASSERT_THAT(first_value, NotNull());

  for (int value = 0; value < 1'024; ++value) {
    const auto inserted = map.try_emplace(std::to_string(value), value);
    ASSERT_THAT(std::get_if<Insertion>(&inserted), NotNull());
  }
  EXPECT_THAT(map.mapped(first_id), Eq(first_value));
  EXPECT_THAT(*map.mapped(first_id), Eq(1));
  const auto entry = map.get(first_id);
  ASSERT_THAT(entry.has_value(), Eq(true));
  // NOLINTBEGIN(bugprone-unchecked-optional-access): ASSERT_THAT above terminates this test on failure.
  EXPECT_THAT(entry->key, Eq("first"));
  EXPECT_THAT(entry->mapped, Eq(1));
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_F(StringInternerMapTest, ChildPreservesCapturedParentValuesAndCanAddPostCutoffKey) {
  StringInternerMap<int> parent;
  const auto inherited = parent.try_emplace("inherited", 11);
  const auto inherited_id = std::get<Insertion>(inherited).first;
  StringInternerMap<int> child(&parent);
  const auto duplicate = child.try_emplace("inherited", 99);
  EXPECT_THAT(duplicate, VariantWith<Insertion>(Eq(Insertion(inherited_id, false))));
  EXPECT_THAT(child.mapped(inherited_id), NotNull());
  EXPECT_THAT(*child.mapped(inherited_id), Eq(11));

  const auto later_parent = parent.try_emplace("later", 22);
  ASSERT_THAT(std::get_if<Insertion>(&later_parent), NotNull());
  EXPECT_THAT(child.find("later"), Eq(std::nullopt));
  const auto local = child.try_emplace("later", 33);
  ASSERT_THAT(std::get_if<Insertion>(&local), NotNull());
  const auto local_id = std::get<Insertion>(local).first;
  EXPECT_THAT(child.mapped(local_id), NotNull());
  EXPECT_THAT(*child.mapped(local_id), Eq(33));

  auto forward = child.begin();
  EXPECT_THAT((*forward).key, Eq("inherited"));
  EXPECT_THAT((*forward++).mapped, Eq(11));
  EXPECT_THAT((*forward).key, Eq("later"));
  EXPECT_THAT((*forward++).mapped, Eq(33));
  EXPECT_THAT(forward == child.end(), Eq(true));
  auto reverse = child.rbegin();
  EXPECT_THAT((*reverse).key, Eq("later"));
  EXPECT_THAT((*reverse++).mapped, Eq(33));
  EXPECT_THAT((*reverse).key, Eq("inherited"));
  EXPECT_THAT((*reverse++).mapped, Eq(11));
  EXPECT_THAT(reverse == child.rend(), Eq(true));
}

TEST_F(StringInternerMapTest, MappedExhaustionDoesNotPublishAString) {
  using Values = mbo::container::LimitedVector<int, 1>;
  StringInternerMap<int, StringInterner<>, Values> map;
  const auto first = map.try_emplace("one", 1);
  ASSERT_THAT(std::get_if<Insertion>(&first), NotNull());
  EXPECT_THAT(map.try_emplace("two", 2), VariantWith<StringInternError>(StringInternError::kMappedStorageExhausted));
  EXPECT_THAT(map.find("two"), Eq(std::nullopt));
  EXPECT_THAT(map.size(), Eq(1));
}

TEST_F(StringInternerMapTest, IteratorBoundariesSupportEmptyMapsAndDecrementingEnd) {
  const StringInternerMap<int>::iterator first_singular;
  const StringInternerMap<int>::iterator second_singular;
  EXPECT_THAT(first_singular == second_singular, Eq(true));
  StringInternerMap<int> map;
  EXPECT_THAT(map.begin() == map.end(), Eq(true));
  EXPECT_THAT(map.rbegin() == map.rend(), Eq(true));
  EXPECT_THAT(map.try_emplace("entry", 1).index(), Eq(0));
  auto position = map.end();
  EXPECT_THAT((*--position).key, Eq("entry"));
  EXPECT_THAT((*position).mapped, Eq(1));
  EXPECT_THAT(position == map.begin(), Eq(true));
  EXPECT_THAT(++position == map.end(), Eq(true));

  StringInternerMap<int> other;
  EXPECT_THAT(other.try_emplace("entry", 1).index(), Eq(0));
  EXPECT_THAT(map.begin() == other.begin(), Eq(false));
}

TEST_F(StringInternerMapTest, InvalidIdentifiersHaveNeitherKeysNorMappedValues) {
  const StringInternerMap<int> map;
  const auto invalid = StringId<>(0);
  EXPECT_THAT(map.key(invalid), Eq(std::nullopt));
  EXPECT_THAT(map.mapped(invalid), Eq(nullptr));
  EXPECT_THAT(map.get(invalid), Eq(std::nullopt));
}

#ifndef NDEBUG
TEST_F(StringInternerMapTest, InvalidIteratorOperationsFailDebugContracts) {
  const StringInternerMap<int> map;
  const StringInternerMap<int>::iterator singular;
  EXPECT_DEATH(static_cast<void>(*singular), "singular StringInternerMap iterator");
  EXPECT_DEATH(static_cast<void>(*map.end()), "StringInternerMap end iterator");
  EXPECT_DEATH(static_cast<void>(++map.end()), "StringInternerMap end iterator");
  EXPECT_DEATH(static_cast<void>(--map.begin()), "StringInternerMap begin iterator");
}
#endif

TEST_F(StringInternerMapTest, EveryStorageLayerCanUseCallerOwnedBoundedMemory) {
  constexpr mbo::container::SegmentedSequenceOptions kMappedSequenceOptions{
      .segment_capacities = {5}, .repeat_last = false, .maximum_size = 5};
  constexpr mbo::memory::ArenaOptions kArenaOptions{.initial_block_size = 512, .maximum_block_size = 512};
  using CharacterArena = mbo::memory::Arena<mbo::memory::InlineBlockSource<1'024>, kArenaOptions>;
  using Storage = ArenaStringStorage<CharacterArena>;
  using Entries =
      mbo::container::SegmentedSequence<std::string_view, kMappedSequenceOptions, mbo::memory::InlineBlockSource<256>>;
  using NodeArena = mbo::memory::Arena<mbo::memory::InlineBlockSource<8'192>, kArenaOptions>;
  using NodeSource = mbo::memory::ArenaBlockSource<NodeArena>;
  using Index = HamtStringIndex<
      StringId<>, std::hash<std::string_view>, std::equal_to<>,
      mbo::container::HamtOptions{.maximum_size = 4, .maximum_collision_size = 2}, NodeSource>;
  using Core = StringInterner<std::uint32_t, Storage, Entries, Index, StringInternerOptions{.maximum_parent_depth = 1}>;
  using Values = mbo::container::SegmentedSequence<int, kMappedSequenceOptions, mbo::memory::InlineBlockSource<128>>;
  using Map = StringInternerMap<int, Core, Values>;

  mbo::memory::InlineBlockSource<1'024> control;
  NodeArena node_arena;
  const auto make_core = [&](const Core* parent) noexcept {
    auto index = Index::try_create_in(control, std::hash<std::string_view>{}, std::equal_to<>{}, node_arena);
    if (!index) {
      std::terminate();
    }
    return Core(
        parent, []() noexcept { return Storage{}; }, []() noexcept { return Entries{}; },
        [created = std::move(*index)]() mutable noexcept { return std::move(created); });
  };
  Map map(nullptr, make_core, []() noexcept { return Values{}; });
  for (int value = 0; value < 4; ++value) {
    const auto inserted = map.try_emplace(std::to_string(value), value);
    const auto* const result = std::get_if<Insertion>(&inserted);
    ASSERT_THAT(result, NotNull());
    EXPECT_THAT(result->first, Eq(StringId<>(static_cast<std::uint32_t>(value))));
    EXPECT_THAT(result->second, Eq(true));
  }
  const int* const stable_value = map.mapped(StringId<>(0));
  ASSERT_THAT(stable_value, NotNull());
  for (int attempt = 0; attempt < 8; ++attempt) {
    EXPECT_THAT(map.try_emplace("overflow", 5), VariantWith<StringInternError>(StringInternError::kIndexExhausted));
    EXPECT_THAT(map.mapped(StringId<>(0)), Eq(stable_value));
  }
  EXPECT_THAT(map.size(), Eq(4));
  EXPECT_THAT(map.find("overflow"), Eq(std::nullopt));
  for (int value = 0; value < 4; ++value) {
    const StringId<> identifier(static_cast<std::uint32_t>(value));
    EXPECT_THAT(map.key(identifier), Eq(std::to_string(value)));
    ASSERT_THAT(map.mapped(identifier), NotNull());
    EXPECT_THAT(*map.mapped(identifier), Eq(value));
  }
  const auto mapped_storage = map.local_mapped_storage_diagnostics();
  EXPECT_THAT(mapped_storage.objects, Eq(4));
  EXPECT_THAT(mapped_storage.live_object_bytes, Eq(4 * sizeof(int)));
  EXPECT_THAT(mapped_storage.segment_bytes_reserved.has_value(), Eq(true));
  EXPECT_THAT(map.string_interner().local_index_diagnostics().entries, Eq(4));
  const auto complete_storage = map.local_storage_diagnostics();
  EXPECT_THAT(complete_storage.interner.string_count, Eq(4));
  EXPECT_THAT(complete_storage.interner.character_bytes_used, Optional(std::size_t{4}));
  EXPECT_THAT(complete_storage.interner.index_node_bytes.has_value(), Eq(true));
  EXPECT_THAT(complete_storage.mapped.objects, Eq(4));
  EXPECT_THAT(complete_storage.mapped.live_object_bytes, Eq(4 * sizeof(int)));
}

TEST_F(StringInternerMapTest, StorageDiagnosticsVisitCapturedChainAndMappedDomains) {
  StringInternerMap<int> root;
  EXPECT_THAT(root.try_emplace("root", 1), VariantWith<Insertion>(Pair(StringId<>(0), true)));
  StringInternerMap<int> child(&root);
  EXPECT_THAT(child.try_emplace("child", 2), VariantWith<Insertion>(Pair(StringId<>(1), true)));
  EXPECT_THAT(root.try_emplace("late", 3), VariantWith<Insertion>(Pair(StringId<>(1), true)));

  struct Measurement final {
    std::size_t depth;
    std::size_t visible_strings;
    std::size_t stored_strings;
    std::size_t mapped_objects;
  };

  const auto measurement_is = [](Measurement expected) {
    return AllOf(
        Field("depth", &Measurement::depth, expected.depth),
        Field("visible_strings", &Measurement::visible_strings, expected.visible_strings),
        Field("stored_strings", &Measurement::stored_strings, expected.stored_strings),
        Field("mapped_objects", &Measurement::mapped_objects, expected.mapped_objects));
  };
  std::array<Measurement, 2> measurements{};
  std::size_t count = 0;
  child.visit_storage_diagnostics([&](std::size_t depth, std::size_t visible_strings, const auto& storage) noexcept {
    measurements.at(count++) = {
        .depth = depth,
        .visible_strings = visible_strings,
        .stored_strings = storage.interner.string_count,
        .mapped_objects = storage.mapped.objects,
    };
  });

  EXPECT_THAT(
      measurements, ElementsAre(
                        measurement_is({.depth = 0, .visible_strings = 1, .stored_strings = 1, .mapped_objects = 1}),
                        measurement_is({.depth = 1, .visible_strings = 1, .stored_strings = 2, .mapped_objects = 2})));
}

}  // namespace
}  // namespace mbo::strings
