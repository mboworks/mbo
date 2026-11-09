// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/hash_container_defaults.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/segmented_vector.h"
#include "mbo/strings/arena_string_storage.h"
#include "mbo/strings/string_interner.h"
#include "mbo/strings/string_interner_policies.h"

namespace mbo::strings {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::ResultOf;

struct StringInternerReadmeTest : ::testing::Test {};

TEST_F(StringInternerReadmeTest, DefaultSetOwnsStringsAndReusesDenseIds) {
  using Id = StringIdU32<>;
  StringInternerSet<> strings;
  const auto first = strings.insert("apple");
  const auto again = strings.insert("apple");
  const auto text = strings.get_string(Id{0});
  EXPECT_THAT(first, Eq(std::pair(Id{0}, true)));
  EXPECT_THAT(again, Eq(std::pair(Id{0}, false)));
  EXPECT_THAT(text, Optional(Eq("apple")));
  std::string input = "pear";
  EXPECT_THAT(strings.insert(input), Eq(std::pair(Id{1}, true)));
  input.assign("gone");
  EXPECT_THAT(strings, ElementsAre("apple", "pear"));
}

TEST_F(StringInternerReadmeTest, MapUsesConstParentLinksCapturedMembershipAndSharedMappedValues) {
  using Id = StringIdU32<>;
  using Map = StringInternerMap<Id, int>;
  Map counts;
  const auto inserted = counts.try_emplace("apple", 42);
  EXPECT_THAT(inserted, Eq(std::pair(Id{0}, true)));
  counts.at(Id{0}) += 1;
  const Map* const parent = &counts;
  Map child(parent);
  EXPECT_THAT(counts.try_emplace("late", 7), Eq(std::pair(Id{1}, true)));
  EXPECT_THAT(child.find("late"), Eq(std::nullopt));
  EXPECT_THAT(child.try_emplace("local", 5), Eq(std::pair(Id{1}, true)));
  EXPECT_THAT(child.try_emplace("late", 8), Eq(std::pair(Id{2}, true)));
  child.at(Id{0}) += 1;
  EXPECT_THAT(counts.at(Id{0}), Eq(44));
  EXPECT_THAT(child.at(Id{2}), Eq(8));
  const auto& read = child;
  static_assert(std::same_as<decltype(read.at(Id{0})), const int&>);
  EXPECT_THAT(read.at(Id{0}), Eq(44));
  EXPECT_THAT(child.parent(), Eq(parent));
}

TEST_F(StringInternerReadmeTest, OptionalInsertionAndStlReferenceLookupComposeIndependently) {
  using Id = StringIdU32<>;
  using Hash = absl::DefaultHashContainerHash<std::string_view>;
  using Policy = StringInternerPolicyHolder<StringInternerErrorOptionalPolicy, StringInternerLookupStlPolicy>;
  using Interner = StringInternerSet<
      Id, Hash, absl::flat_hash_map<std::string_view, Id, Hash>, ArenaStringStorage<>,
      mbo::container::SegmentedVector<std::string_view>, Policy>;
  Interner strings;
  EXPECT_THAT(strings.insert("apple"), Optional(Pair(Id{0}, true)));
  EXPECT_THAT(strings.insert("apple"), Optional(Pair(Id{0}, false)));
  EXPECT_THAT(
      strings.get(Id{0}), Optional(ResultOf([](const auto& reference) { return reference.get(); }, Eq("apple"))));
  EXPECT_THAT(strings.get(Id{1}), Eq(std::nullopt));
  EXPECT_THAT(strings.find("apple"), Optional(Id{0}));
}

}  // namespace
}  // namespace mbo::strings
