// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/string_interner.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/strings/arena_string_storage.h"
#include "mbo/strings/internal/string_interner_base.h"
#include "mbo/strings/string_interner_types.h"
#include "mbo/testing/status.h"

namespace mbo::strings {
namespace {
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::Pointee;
using ::testing::SizeIs;

struct StringInternerTest : ::testing::Test {};

// NOLINTBEGIN(readability-identifier-naming): test doubles model the interner's STL-style contracts.
template<std::size_t Capacity>
struct FixedStringStorage final {
  std::optional<std::string_view> try_store(std::string_view text) noexcept {
    if (text.size() > Capacity - size) {
      return std::nullopt;
    }
    if (text.empty()) {
      return std::string_view{};
    }
    auto* const destination = bytes.data() + size;
    std::ranges::copy(text, destination);
    size += text.size();
    return std::string_view(destination, text.size());
  }

  std::array<char, Capacity> bytes{};
  std::size_t size = 0;
};

template<typename Key, typename Id, std::size_t Capacity, bool FailFirst = false>
struct BoundedStringMap final : std::map<Key, Id, std::less<>> {
  using Map = std::map<Key, Id, std::less<>>;

  template<typename... Args>
  std::pair<typename Map::iterator, bool> emplace(Args&&... args) {
    if (std::exchange(fail_next, false) || Map::size() == Capacity) {
      return {Map::end(), false};
    }
    return Map::emplace(std::forward<Args>(args)...);
  }

  bool fail_next = FailFirst;
};

// A container member is not a stability customization point.
struct MarkerOnlyStringMap final : std::map<std::string, StringIdU32<>, std::less<>> {
  static constexpr bool kStringInternerStableKeyAddresses = true;
};

}  // namespace

template<typename Key, typename Id, std::size_t Capacity, bool FailFirst>
inline constexpr bool kHasStableKeyAddresses<BoundedStringMap<Key, Id, Capacity, FailFirst>> = true;

namespace {
template<typename Id, std::size_t Capacity>
using FixedStringContainer = BoundedStringMap<std::string_view, Id, Capacity>;

template<typename Id, std::size_t Capacity, bool FailFirst = false>
using FixedOwningStringContainer = BoundedStringMap<std::string, Id, Capacity, FailFirst>;

struct CollidingHash final {
  using is_transparent = void;

  std::size_t operator()(std::string_view /*key*/) const noexcept {
    ++*calls;
    return value;
  }

  std::size_t* calls;
  std::size_t value;
};

struct MutableStringKeys final {
  using key_type = std::string_view;
  using mapped_type = int;
  using Entry = std::pair<std::string_view, int>;
  using iterator = std::vector<Entry>::iterator;
  using const_iterator = std::vector<Entry>::const_iterator;

  const_iterator find(std::string_view key) const { return std::ranges::find(entries, key, &Entry::first); }

  const_iterator end() const { return entries.end(); }

  std::vector<Entry> entries;
};

template<typename Entry = std::string_view>
struct AppendOnlyEntries final {
  using value_type = Entry;
  using Storage = mbo::container::SegmentedVector<
      Entry,
      mbo::container::SegmentedOptions{.segment_size = 1, .segment_capacity = 1, .segment_reservation = 1}>;

  auto begin() noexcept { return entries.begin(); }

  auto begin() const noexcept { return entries.begin(); }

  auto end() noexcept { return entries.end(); }

  auto end() const noexcept { return entries.end(); }

  std::size_t size() const noexcept { return entries.size(); }

  Entry& back() { return entries.back(); }

  Entry& emplace_back() { return entries.emplace_back(); }

  auto try_emplace_back() { return entries.try_emplace_back(); }

  Storage entries;
};

// NOLINTEND(readability-identifier-naming)

template<
    StringInternerId Id = StringId<>,
    std::size_t CharacterCapacity = 16'384,
    std::size_t StringCapacity = 512,
    typename Entries = mbo::container::SegmentedVector<std::string_view>>
using TestStringInterner = StringInternerSet<
    Id,
    absl::DefaultHashContainerHash<std::string_view>,
    FixedStringContainer<Id, StringCapacity>,
    FixedStringStorage<CharacterCapacity>,
    Entries>;

static_assert(std::bidirectional_iterator<TestStringInterner<>::iterator>);
static_assert(StringInternerId<std::int16_t>);
static_assert(StringInternerId<std::uint16_t>);
static_assert(StringInternerId<StringId<std::int16_t>>);
static_assert(StringInternerId<StringId<std::uint16_t>>);

template<typename Container, typename Storage>
concept SupportsInternerStorage = requires {
  typename strings_internal::StringInternerBase<
      StringIdU32<>, const StringInternerKey<Container>, void, Container, Storage>;
};

template<typename Container, typename Storage>
concept SupportsSetStorage = requires { typename StringInternerSet<StringIdU32<>, void, Container, Storage>; };

template<typename Container, typename Storage>
concept SupportsMapStorage = requires { typename StringInternerMap<StringIdU32<>, int, void, Container, Storage>; };

template<typename Interner>
void ExpectDefaultIndexStability() {
  using Id = Interner::id_type;
  using Inserted = std::pair<Id, bool>;
  Interner interner;
  EXPECT_THAT(interner.find("missing"), Eq(std::nullopt));
  EXPECT_THAT(interner.rfind("missing"), Eq(std::nullopt));
  std::string input("a\0b", 3);
  const std::string long_text(256, 'x');
  EXPECT_THAT(interner.insert(input), Eq(Inserted{Id{0}, true}));
  EXPECT_THAT(interner.insert(long_text), Eq(Inserted{Id{1}, true}));
  const auto original_short = interner.get_string(Id{0}).value_or(std::string_view{});
  const auto original_long = interner.get_string(Id{1}).value_or(std::string_view{});
  const void* const short_address = original_short.data();
  const void* const long_address = original_long.data();
  const auto position = interner.begin();
  input.assign("changed");
  for (std::uint32_t ordinal = 2; ordinal < 258; ++ordinal) {
    EXPECT_THAT(interner.insert(std::to_string(ordinal)), Eq(Inserted{Id{ordinal}, true}));
  }
  EXPECT_THAT(interner, SizeIs(258));
  EXPECT_THAT(original_short, Eq(std::string_view("a\0b", 3)));
  EXPECT_THAT(original_long, Eq(long_text));
  EXPECT_THAT(*position, Eq(original_short));
  EXPECT_THAT(
      static_cast<const void*>(interner.get_string(Id{0}).value_or(std::string_view{}).data()), Eq(short_address));
  EXPECT_THAT(
      static_cast<const void*>(interner.get_string(Id{1}).value_or(std::string_view{}).data()), Eq(long_address));
  EXPECT_THAT(interner.find(original_short), Optional(Id{0}));
  EXPECT_THAT(interner.find(long_text), Optional(Id{1}));
  EXPECT_THAT(interner.rfind(original_short), Optional(Id{0}));
  EXPECT_THAT(interner.find("missing"), Eq(std::nullopt));
  EXPECT_THAT(interner.rfind("missing"), Eq(std::nullopt));
  EXPECT_THAT(interner.insert(original_short), Eq(Inserted{Id{0}, false}));
}

TEST_F(StringInternerTest, BaseArenaAndFlatIndexPreserveViewsThroughGrowth) {
  using Base = strings_internal::StringInternerBase<>;
  static_assert(std::same_as<Base::id_type, StringIdU32<>>);
  static_assert(
      std::same_as<Base::insertion_result, std::expected<std::pair<StringIdU32<>, bool>, StringInternerError>>);
  ExpectDefaultIndexStability<Base>();
}

TEST_F(StringInternerTest, CapacityReportsDenseEntryCapacity) {
  StringInternerSet<> interner;
  EXPECT_THAT(interner.local_capacity(), Eq(0));
  EXPECT_THAT(interner.capacity(), Eq(0));
  ASSERT_THAT(interner.insert("value").has_value(), Eq(true));
  EXPECT_THAT(interner.local_capacity(), ::testing::Ge(interner.local_size()));
  EXPECT_THAT(interner.capacity(), Eq(interner.local_capacity()));
}

TEST_F(StringInternerTest, StlResultPolicyUsesReferenceWrappersForLookup) {
  using Base = strings_internal::StringInternerBase<
      StringIdU32<>, const std::string_view, absl::DefaultHashContainerHash<std::string_view>,
      absl::flat_hash_map<std::string_view, StringIdU32<>>, ArenaStringStorage<>,
      strings_internal::StringInternerEntriesFor<const std::string_view>,
      StringInternerPolicyHolder<StringInternerErrorStlPolicy, StringInternerLookupStlPolicy>>;
  static_assert(std::same_as<Base::lookup_result, std::optional<std::reference_wrapper<const std::string_view>>>);
  static_assert(std::same_as<Base::const_lookup_result, std::optional<std::reference_wrapper<const std::string_view>>>);
  Base interner;
  ASSERT_THAT(interner.insert("value").has_value(), Eq(true));
  ASSERT_THAT(interner.get(StringIdU32<>(0)).has_value(), Eq(true));
  EXPECT_THAT(interner.get(StringIdU32<>(0))->get(), Eq(std::string_view("value")));
}

TEST_F(StringInternerTest, OptionalErrorPolicyDropsDetailedInsertionErrors) {
  using Policy = StringInternerPolicyHolder<StringInternerErrorOptionalPolicy, StringInternerLookupStlPolicy>;
  using Base = strings_internal::StringInternerBase<
      StringIdU32<>, const std::string_view, absl::DefaultHashContainerHash<std::string_view>,
      absl::flat_hash_map<std::string_view, StringIdU32<>>, ArenaStringStorage<>,
      strings_internal::StringInternerEntriesFor<const std::string_view>, Policy>;
  static_assert(std::same_as<Base::insertion_result, std::optional<std::pair<StringIdU32<>, bool>>>);
}

TEST_F(StringInternerTest, DefaultSetWrapperPreservesViewsThroughGrowth) {
  ExpectDefaultIndexStability<StringInternerSet<>>();
}

TEST_F(StringInternerTest, KeyOnlySetIndexPreservesViewsThroughGrowth) {
  using Hash = absl::DefaultHashContainerHash<std::string_view>;
  ExpectDefaultIndexStability<StringInternerSet<StringIdU32<>, Hash, absl::flat_hash_set<std::string_view, Hash>>>();
}

template<typename Interner>
void ExpectInsertionAndConversionAcrossParents() {
  using Id = Interner::id_type;
  using Inserted = std::pair<Id, bool>;
  using Result = Interner::insertion_result;
  static_assert(std::same_as<Result, std::expected<Inserted, StringInternerError>>);
  Interner parent;
  EXPECT_THAT(parent.insert("shared"), Eq(Result(std::in_place, Id{0}, true)));
  EXPECT_THAT(ToStatusOr(parent.insert("shared")), IsOkAndHolds(Inserted{Id{0}, false}));
  Interner child(&parent);
  EXPECT_THAT(child.insert("shared"), Eq(Result(std::in_place, Id{0}, false)));
  EXPECT_THAT(ToStatusOr(child.insert("local")), IsOkAndHolds(Inserted{Id{1}, true}));
  EXPECT_THAT(child.insert("local"), Eq(Result(std::in_place, Id{1}, false)));
  EXPECT_THAT(parent.insert("later"), Eq(Inserted{Id{1}, true}));
  EXPECT_THAT(child.insert("later"), Eq(Result(std::in_place, Id{2}, true)));
  EXPECT_THAT(child.insert("later"), Eq(Inserted{Id{2}, false}));
  EXPECT_THAT(child, ElementsAre("shared", "local", "later"));
  EXPECT_THAT(parent, ElementsAre("shared", "later"));
}

TEST_F(StringInternerTest, SetWrapperPreservesIdsAndParentVisibilityThroughStatusConversion) {
  ExpectInsertionAndConversionAcrossParents<StringInternerSet<>>();
}

TEST_F(StringInternerTest, KeyOnlySetIndexPreservesIdsAndParentVisibilityThroughStatusConversion) {
  using Hash = absl::DefaultHashContainerHash<std::string_view>;
  ExpectInsertionAndConversionAcrossParents<
      StringInternerSet<StringIdU32<>, Hash, absl::flat_hash_set<std::string_view, Hash>>>();
}

TEST_F(StringInternerTest, ErrorCodesConvertToStatusesWithoutMessages) {
  using Result = StringInternerSet<>::insertion_result;
  static constexpr auto kCases = std::to_array<std::pair<StringInternerError, absl::StatusCode>>({
      {StringInternerError::kCharacterStorageExhausted, absl::StatusCode::kResourceExhausted},
      {StringInternerError::kEntryStorageExhausted, absl::StatusCode::kResourceExhausted},
      {StringInternerError::kIdExhausted, absl::StatusCode::kOutOfRange},
      {StringInternerError::kIndexExhausted, absl::StatusCode::kResourceExhausted},
  });
  for (const auto& [error, code] : kCases) {
    EXPECT_THAT(ToStatusCode(error), Eq(code));
    EXPECT_THAT(ToStatusOr(Result(std::unexpected(error))), StatusIs(code, ""));
  }
}

template<typename Entry, typename Matcher>
void ExpectMapEntry(const Entry& entry, const Matcher& matcher) {
  ASSERT_THAT(entry.has_value(), Eq(true));
  EXPECT_THAT(*entry, matcher);
}

template<typename Interner>
concept HasStringAt = requires(Interner& interner, std::string_view key) { interner.at(key); };

template<typename Interner>
concept HasStringSubscript = requires(Interner& interner, std::string_view key) { interner[key]; };

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): exercise checked ID subscripting.
TEST_F(StringInternerTest, MapStoresCallerValuesAndExposesMapElementTypes) {
  using Interner = StringInternerMap<std::uint32_t, std::string>;
  using Inserted = std::pair<std::uint32_t, bool>;
  static_assert(std::same_as<Interner::key_type, std::string_view>);
  static_assert(std::same_as<Interner::mapped_type, std::string>);
  static_assert(std::same_as<Interner::value_type, std::pair<const std::string_view, std::string>>);
  static_assert(std::same_as<Interner::element_type, Interner::value_type>);
  static_assert(std::same_as<std::iter_value_t<Interner::iterator>, Interner::element_type>);
  static_assert(std::same_as<Interner::reference, Interner::element_type&>);
  static_assert(std::same_as<Interner::const_reference, const Interner::element_type&>);
  static_assert(std::same_as<std::iter_reference_t<Interner::iterator>, Interner::element_type&>);
  static_assert(std::same_as<std::iter_reference_t<Interner::const_iterator>, const Interner::element_type&>);
  static_assert(std::bidirectional_iterator<Interner::iterator>);
  static_assert(std::bidirectional_iterator<Interner::const_iterator>);
  static_assert(std::convertible_to<Interner::iterator, Interner::const_iterator>);
  static_assert(!std::convertible_to<Interner::const_iterator, Interner::iterator>);
  static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<Interner>().begin()->first)>>);
  static_assert(!std::is_const_v<std::remove_reference_t<decltype((std::declval<Interner>().begin()->second))>>);
  static_assert(std::is_const_v<std::remove_reference_t<decltype((std::declval<const Interner>().begin()->second))>>);
  Interner interner;
  EXPECT_THAT(interner.insert("first", "one"), Eq(Inserted{0, true}));
  EXPECT_THAT(interner.try_emplace("second", 3, 'x'), Eq(Inserted{1, true}));
  EXPECT_THAT(interner.insert("first", "replacement"), Eq(Inserted{0, false}));
  ExpectMapEntry(interner.get(0), Pair("first", "one"));
  ExpectMapEntry(interner.get(1), Pair("second", "xxx"));
  EXPECT_THAT(interner.find("second"), Optional(1));
  EXPECT_THAT(interner.rfind("first"), Optional(0));
  EXPECT_THAT(interner.find("missing").has_value(), Eq(false));
  EXPECT_THAT(interner.get(2).has_value(), Eq(false));
  EXPECT_THAT(interner, ElementsAre(Pair("first", "one"), Pair("second", "xxx")));
  EXPECT_THAT(interner.begin()->second, Eq("one"));
  EXPECT_THAT(interner.rbegin()->second, Eq("xxx"));
}

TEST_F(StringInternerTest, MapLookupAndIteratorsMutateValuesWithoutChangingKeysOrIds) {
  using Interner = StringInternerMap<std::uint32_t, std::string>;
  using Inserted = std::pair<std::uint32_t, bool>;
  static_assert(std::is_const_v<Interner::element_type::first_type>);
  static_assert(!std::is_const_v<Interner::element_type::second_type>);
  static_assert(!std::is_assignable_v<Interner::element_type&, Interner::element_type>);
  Interner interner;
  ASSERT_THAT(interner.insert("first", "one"), Eq(Inserted{0, true}));
  ASSERT_THAT(interner.insert("second", "two"), Eq(Inserted{1, true}));
  auto by_id = interner.get(0);
  ASSERT_THAT(by_id.has_value(), Eq(true));
  by_id->second = "id";
  auto by_key = interner.get(1);
  ASSERT_THAT(by_key.has_value(), Eq(true));
  by_key->second = "key";
  EXPECT_THAT(interner, ElementsAre(Pair("first", "id"), Pair("second", "key")));
  interner.begin()->second += " forward";
  interner.rbegin()->second += " reverse";
  EXPECT_THAT(interner, ElementsAre(Pair("first", "id forward"), Pair("second", "key reverse")));
  EXPECT_THAT(interner.find("first"), Optional(0));
  EXPECT_THAT(interner.find("second"), Optional(1));
  EXPECT_THAT(interner.insert("first", "replacement"), Eq(Inserted{0, false}));
  EXPECT_THAT(interner.at(0), Eq("id forward"));
}

TEST_F(StringInternerTest, ConstMapAccessAndIteratorConversionsPreserveConstness) {
  using Interner = StringInternerMap<std::uint32_t, int>;
  using Inserted = std::pair<std::uint32_t, bool>;
  static_assert(std::same_as<decltype(std::declval<Interner&>().get(0)), Interner::lookup_result>);
  static_assert(std::same_as<decltype(std::declval<const Interner&>().get(0)), Interner::const_lookup_result>);
  static_assert(std::is_const_v<std::remove_reference_t<decltype((std::declval<const Interner&>().get(0)->second))>>);
  static_assert(std::same_as<decltype(std::declval<const Interner&>().at(0)), const int&>);
  static_assert(std::same_as<decltype(std::declval<const Interner&>()[0]), const int&>);
  Interner interner;
  ASSERT_THAT(interner.insert("first", 10), Eq(Inserted{0, true}));
  ASSERT_THAT(interner.insert("second", 20), Eq(Inserted{1, true}));
  const Interner& read = interner;
  Interner::const_iterator position = interner.begin();
  EXPECT_THAT(position == interner.begin(), Eq(true));
  EXPECT_THAT(interner.begin() == position, Eq(true));
  EXPECT_THAT(position, Eq(read.cbegin()));
  EXPECT_THAT(*position++, Pair("first", 10));
  EXPECT_THAT(*position++, Pair("second", 20));
  EXPECT_THAT(position == interner.end(), Eq(true));
  EXPECT_THAT(position, Eq(read.cend()));
  EXPECT_THAT(std::ranges::subrange(read.rbegin(), read.rend()), ElementsAre(Pair("second", 20), Pair("first", 10)));
  EXPECT_THAT(read.crbegin(), Eq(interner.rbegin()));
  EXPECT_THAT(read.crend(), Eq(interner.rend()));
  ExpectMapEntry(read.get(0), Pair("first", 10));
  EXPECT_THAT(read.find("missing").has_value(), Eq(false));
  EXPECT_THAT(read[1], Eq(20));
  EXPECT_THAT(read.at(0), Eq(10));
}

TEST_F(StringInternerTest, MapAtAndSubscriptAccessExistingValuesById) {
  using Interner = StringInternerMap<std::uint32_t, std::string>;
  using Inserted = std::pair<std::uint32_t, bool>;
  static_assert(std::same_as<decltype(std::declval<Interner&>().at(0)), std::string&>);
  static_assert(std::same_as<decltype(std::declval<Interner&>()[0]), std::string&>);
  static_assert(!HasStringAt<Interner>);
  static_assert(!HasStringAt<const Interner>);
  static_assert(!HasStringSubscript<Interner>);
  static_assert(!HasStringSubscript<const Interner>);
  Interner interner;
  ASSERT_THAT(interner.insert("first", "one"), Eq(Inserted{0, true}));
  ASSERT_THAT(interner.insert("second", "two"), Eq(Inserted{1, true}));
  interner.at(0) += " at";
  interner[0] += " index";
  interner[1] = "changed";
  EXPECT_THAT(interner, ElementsAre(Pair("first", "one at index"), Pair("second", "changed")));
  EXPECT_THAT(interner.find("first"), Optional(0));
  EXPECT_THAT(interner.find("second"), Optional(1));
  EXPECT_THAT(interner, SizeIs(2));
}

TEST_F(StringInternerTest, MapValueMutationsAreSharedThroughTheCapturedParentPrefix) {
  using Interner = StringInternerMap<std::uint32_t, int>;
  using Inserted = std::pair<std::uint32_t, bool>;
  static_assert(std::constructible_from<Interner, Interner*>);
  static_assert(std::constructible_from<Interner, const Interner*>);
  Interner root;
  ASSERT_THAT(root.insert("root", 1), Eq(Inserted{0, true}));
  Interner child(&root);
  ASSERT_THAT(child.insert("child", 2), Eq(Inserted{1, true}));
  Interner grandchild(&child);
  Interner sibling(&root);
  grandchild.at(0) = 10;
  grandchild.rbegin()->second = 20;
  EXPECT_THAT(root.at(0), Eq(10));
  EXPECT_THAT(sibling[0], Eq(10));
  EXPECT_THAT(child.at(1), Eq(20));
  ASSERT_THAT(root.insert("later", 30), Eq(Inserted{1, true}));
  ASSERT_THAT(child.insert("later", 40), Eq(Inserted{2, true}));
  EXPECT_THAT(grandchild.find("later").has_value(), Eq(false));
  ASSERT_THAT(grandchild.insert("later", 50), Eq(Inserted{2, true}));
  grandchild[0] += 1;
  EXPECT_THAT(root[0], Eq(11));
  EXPECT_THAT(root[1], Eq(30));
  EXPECT_THAT(child[2], Eq(40));
  EXPECT_THAT(grandchild, ElementsAre(Pair("root", 11), Pair("child", 20), Pair("later", 50)));
}

struct StringInternerDeathTest : StringInternerTest {};

TEST_F(StringInternerDeathTest, MapAtAndSubscriptRejectInvalidIds) {
  using Inserted = std::pair<std::int32_t, bool>;
  StringInternerMap<std::int32_t, int> interner;
  ASSERT_THAT(interner.insert("first", 10), Eq(Inserted{0, true}));
  EXPECT_DEATH(static_cast<void>(interner.at(-1)), "String interner ID out of range");
  EXPECT_DEATH(static_cast<void>(interner[1]), "String interner ID out of range");
  const auto& read = interner;
  EXPECT_DEATH(static_cast<void>(read.at(1)), "String interner ID out of range");
  EXPECT_DEATH(static_cast<void>(read[-1]), "String interner ID out of range");
}

TEST_F(StringInternerTest, MapConvertsInsertionResultsExplicitly) {
  using Interner = StringInternerMap<std::uint32_t, std::string>;
  using Inserted = std::pair<std::uint32_t, bool>;
  Interner interner;
  EXPECT_THAT(ToStatusOr(interner.insert("first", "one")), IsOkAndHolds(Inserted{0, true}));
  EXPECT_THAT(interner.insert("second", "two"), Eq(Inserted{1, true}));
  EXPECT_THAT(ToStatusOr(interner.insert("first", "one")), IsOkAndHolds(Inserted{0, false}));
  EXPECT_THAT(interner, ElementsAre(Pair("first", "one"), Pair("second", "two")));
}

TEST_F(StringInternerTest, MapDuplicatesDoNotConsumeMoveOnlyValuesOrConstructReplacements) {
  using Interner = StringInternerMap<std::uint32_t, std::unique_ptr<int>>;
  using Inserted = std::pair<std::uint32_t, bool>;
  Interner parent;
  EXPECT_THAT(parent.insert("shared", std::make_unique<int>(42)), Eq(Inserted{0, true}));
  Interner child(&parent);
  auto unused = std::make_unique<int>(99);
  EXPECT_THAT(child.insert("shared", std::move(unused)), Eq(Inserted{0, false}));
  EXPECT_THAT(unused, Pointee(Eq(99)));
  EXPECT_THAT(child.try_emplace("shared", nullptr), Eq(Inserted{0, false}));
  ExpectMapEntry(child.get(0), Pair("shared", Pointee(Eq(42))));
  EXPECT_THAT(child.insert("local", std::move(unused)), Eq(Inserted{1, true}));
  EXPECT_THAT(unused, Eq(nullptr));
  ExpectMapEntry(child.get(1), Pair("local", Pointee(Eq(99))));
}

struct InPlaceValue final {
  explicit InPlaceValue(int value, int& constructions) : value(value) { ++constructions; }

  InPlaceValue(const InPlaceValue&) = delete;
  InPlaceValue& operator=(const InPlaceValue&) = delete;
  InPlaceValue(InPlaceValue&&) = delete;
  InPlaceValue& operator=(InPlaceValue&&) = delete;
  ~InPlaceValue() = default;

  int value;
};

TEST_F(StringInternerTest, MapEmplacesImmovableValuesOnlyForNewKeys) {
  using Map = std::map<std::string_view, std::uint32_t, std::less<>>;
  using Interner = StringInternerMap<std::uint32_t, InPlaceValue, void, Map, FixedStringStorage<5>>;
  using Inserted = std::pair<std::uint32_t, bool>;
  // NOLINTNEXTLINE(misc-const-correctness): The mapped constructor increments this forwarded reference.
  int constructions = 0;
  Interner interner;
  EXPECT_THAT(interner.try_emplace("first", 42, constructions), Eq(Inserted{0, true}));
  EXPECT_THAT(interner.try_emplace("first", 99, constructions), Eq(Inserted{0, false}));
  EXPECT_THAT(
      interner.try_emplace("full", 100, constructions),
      Eq(std::unexpected(StringInternerError::kCharacterStorageExhausted)));
  EXPECT_THAT(constructions, Eq(1));
  ASSERT_THAT(interner.get(0).has_value(), Eq(true));
  EXPECT_THAT(interner.get(0)->second.value, Eq(42));
  interner.at(0).value = 43;
  interner[0].value = 44;
  EXPECT_THAT(std::as_const(interner)[0].value, Eq(44));
}

TEST_F(StringInternerTest, MapValuesFollowCapturedParentIdsAndReverseIteration) {
  using Interner = StringInternerMap<std::int32_t, int>;
  using Inserted = std::pair<std::int32_t, bool>;
  Interner root;
  EXPECT_THAT(root.insert("root", 101), Eq(Inserted{0, true}));
  Interner child(&root);
  EXPECT_THAT(child.insert("child", -7), Eq(Inserted{1, true}));
  Interner grandchild(&child);
  EXPECT_THAT(child.insert("later", 300), Eq(Inserted{2, true}));
  EXPECT_THAT(root.insert("later", 400), Eq(Inserted{1, true}));
  EXPECT_THAT(grandchild.find("later").has_value(), Eq(false));
  EXPECT_THAT(grandchild.get(2).has_value(), Eq(false));
  EXPECT_THAT(grandchild.insert("later", 500), Eq(Inserted{2, true}));
  ExpectMapEntry(grandchild.get(0), Pair("root", 101));
  ExpectMapEntry(grandchild.get(1), Pair("child", -7));
  EXPECT_THAT(grandchild.get(-1).has_value(), Eq(false));
  ExpectMapEntry(grandchild.get(2), Pair("later", 500));
  ExpectMapEntry(child.get(2), Pair("later", 300));
  ExpectMapEntry(root.get(1), Pair("later", 400));
  EXPECT_THAT(grandchild, ElementsAre(Pair("root", 101), Pair("child", -7), Pair("later", 500)));
  EXPECT_THAT(
      std::ranges::subrange(grandchild.rbegin(), grandchild.rend()),
      ElementsAre(Pair("later", 500), Pair("child", -7), Pair("root", 101)));
}

template<typename Interner>
void ExpectMapGrowth() {
  using Id = Interner::id_type;
  using Inserted = std::pair<Id, bool>;
  Interner interner;
  std::string input("a\0b", 3);
  EXPECT_THAT(interner.insert(input, 900), Eq(Inserted{Id{0}, true}));
  const auto position = interner.begin();
  const std::string_view original = interner.get_string(Id{0}).value_or(std::string_view{});
  const void* const address = original.data();
  input.assign("changed");
  for (std::uint32_t ordinal = 1; ordinal < 258; ++ordinal) {
    EXPECT_THAT(interner.insert(std::to_string(ordinal), 1'000 + ordinal), Eq(Inserted{Id{ordinal}, true}));
  }
  EXPECT_THAT(original, Eq(std::string_view("a\0b", 3)));
  EXPECT_THAT(*position, Pair(original, 900));
  ExpectMapEntry(interner.get(Id{0}), Pair(original, 900));
  EXPECT_THAT(static_cast<const void*>(interner.get_string(Id{0}).value_or(std::string_view{}).data()), Eq(address));
  ExpectMapEntry(interner.get(Id{257}), Pair("257", 1'257));
  EXPECT_THAT(interner.insert(original, 1), Eq(Inserted{Id{0}, false}));
}

TEST_F(StringInternerTest, MapStringViewsAndOrdinalIteratorsSurviveFlatMapGrowth) {
  ExpectMapGrowth<StringInternerMap<StringIdU32<>, std::uint32_t>>();
}

TEST_F(StringInternerTest, MapCanOwnStringKeysWithoutSeparateCharacterStorage) {
  using Map = absl::node_hash_map<std::string, StringIdU32<>>;
  ExpectMapGrowth<StringInternerMap<StringIdU32<>, std::uint32_t, void, Map>>();
}

TEST_F(StringInternerTest, MapSupportsConstOwningKeysAndCustomHashCollisions) {
  using Id = StringIdU32<>;
  using Inserted = std::pair<Id, bool>;
  using Map = std::map<const std::string, Id, std::less<>>;
  StringInternerMap<Id, std::string, void, Map> owning;
  EXPECT_THAT(owning.insert("first", "one"), Eq(Inserted{Id{0}, true}));
  ExpectMapEntry(owning.get(Id{0}), Pair("first", "one"));

  std::size_t calls = 0;
  StringInternerMap<Id, int, CollidingHash> interner(nullptr, 0, CollidingHash{.calls = &calls, .value = 7});
  EXPECT_THAT(interner.insert("first", 10), Eq(Inserted{Id{0}, true}));
  EXPECT_THAT(interner.insert("second", 20), Eq(Inserted{Id{1}, true}));
  ExpectMapEntry(interner.get(Id{0}), Pair("first", 10));
  ExpectMapEntry(interner.get(Id{1}), Pair("second", 20));
  EXPECT_THAT(calls, Gt(0));
}

TEST_F(StringInternerTest, SetAndMapExposeTheSameStringKeysAndSeparateIteratorIds) {
  using Id = StringIdU32<>;
  using Set = StringInternerSet<Id>;
  using Map = StringInternerMap<Id, int>;
  static_assert(std::same_as<Set::key_type, Map::key_type>);
  static_assert(std::same_as<Set::element_type, const std::string_view>);
  static_assert(std::same_as<Set::reference, const Set::key_type&>);
  Set strings;
  Map values;
  ASSERT_THAT(strings.insert("root").has_value(), Eq(true));
  ASSERT_THAT(values.insert("root", 10).has_value(), Eq(true));
  Set child_strings(&strings);
  Map child_values(&values);
  ASSERT_THAT(child_strings.insert("child").has_value(), Eq(true));
  ASSERT_THAT(child_values.insert("child", 20).has_value(), Eq(true));
  auto key = child_strings.begin();
  auto entry = child_values.begin();
  EXPECT_THAT(*key, Eq(entry->first));
  EXPECT_THAT(key.id(), Eq(Id{0}));
  EXPECT_THAT(entry.id(), Eq(key.id()));
  ++key;
  ++entry;
  EXPECT_THAT(*key, Eq(entry->first));
  EXPECT_THAT(key.id(), Eq(Id{1}));
  EXPECT_THAT(entry.id(), Eq(key.id()));
  const Map::const_iterator read = entry;
  EXPECT_THAT(read.id(), Eq(Id{1}));
  EXPECT_THAT(child_values.at(read.id()), Eq(20));
}

TEST_F(StringInternerTest, IdAccessAndIterationDoNotHashAndValuesSurviveIndexGrowth) {
  using Map = StringInternerMap<std::uint32_t, int, CollidingHash>;
  std::size_t calls = 0;
  Map values(nullptr, 0, CollidingHash{.calls = &calls, .value = 7});
  ASSERT_THAT(values.insert("first", 10).has_value(), Eq(true));
  auto first = values.get(0);
  ASSERT_THAT(first.has_value(), Eq(true));
  int* const address = &first->second;
  for (std::uint32_t ordinal = 1; ordinal < 258; ++ordinal) {
    ASSERT_THAT(values.insert(std::to_string(ordinal), 20).has_value(), Eq(true));
  }
  const auto calls_before = calls;
  EXPECT_THAT(&values.at(0), Eq(address));
  EXPECT_THAT(values[0], Eq(10));
  EXPECT_THAT(values.get_string(0), Optional(Eq("first")));
  EXPECT_THAT(values.begin()->first, Eq("first"));
  EXPECT_THAT(values.begin().id(), Eq(0));
  EXPECT_THAT(values.rbegin()->first, Eq("257"));
  EXPECT_THAT(calls, Eq(calls_before));
}

TEST_F(StringInternerTest, OwningSetAndMapExposeConstStringReferences) {
  using Index = absl::node_hash_map<std::string, std::uint32_t>;
  using Set = StringInternerSet<std::uint32_t, void, Index>;
  using Map = StringInternerMap<std::uint32_t, int, void, Index>;
  static_assert(std::same_as<Set::key_type, std::string>);
  static_assert(std::same_as<Map::key_type, Set::key_type>);
  static_assert(std::same_as<Set::reference, const std::string&>);
  static_assert(std::same_as<Map::element_type, std::pair<const std::string&, int>>);
  static_assert(!std::is_assignable_v<decltype((std::declval<Map&>().begin()->first)), std::string>);
  static_assert(std::bidirectional_iterator<Set::iterator>);
  static_assert(std::bidirectional_iterator<Map::iterator>);
  Set strings;
  Map values;
  ASSERT_THAT(strings.insert("short").has_value(), Eq(true));
  ASSERT_THAT(values.insert("short", 10).has_value(), Eq(true));
  const std::string& set_key = *strings.begin();
  const std::string& map_key = values.begin()->first;
  for (std::uint32_t ordinal = 1; ordinal < 258; ++ordinal) {
    ASSERT_THAT(strings.insert(std::to_string(ordinal)).has_value(), Eq(true));
    ASSERT_THAT(values.insert(std::to_string(ordinal), 20).has_value(), Eq(true));
  }
  EXPECT_THAT(std::addressof(*strings.begin()), Eq(std::addressof(set_key)));
  EXPECT_THAT(std::addressof(values.begin()->first), Eq(std::addressof(map_key)));
  EXPECT_THAT(strings.get_string(0).value_or(std::string_view{}).data(), Eq(set_key.data()));
  EXPECT_THAT(values.get_string(0).value_or(std::string_view{}).data(), Eq(map_key.data()));
  values.begin()->second = 30;
  EXPECT_THAT(values.at(0), Eq(30));
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

TEST_F(StringInternerTest, MapAcceptsKeyOnlyIndexesAndVectorElementStorage) {
  using Id = StringIdU32<>;
  using Hash = absl::DefaultHashContainerHash<std::string_view>;
  using Entries = std::vector<std::optional<std::pair<const std::string_view, std::uint32_t>>>;
  ExpectMapGrowth<StringInternerMap<
      Id, std::uint32_t, Hash, absl::flat_hash_set<std::string_view>, ArenaStringStorage<>, Entries>>();
  ExpectMapGrowth<StringInternerMap<Id, std::uint32_t, void, std::set<std::string, std::less<>>>>();
}

TEST_F(StringInternerTest, MapIndexFailureDoesNotPublishAValueAndRetryReusesTheId) {
  using Map = BoundedStringMap<std::string, std::uint32_t, 1, true>;
  using Interner = StringInternerMap<
      std::uint32_t, int, void, Map, void, AppendOnlyEntries<std::optional<std::pair<const std::string&, int>>>>;
  using Inserted = std::pair<std::uint32_t, bool>;
  Interner interner;
  EXPECT_THAT(interner.insert("failed", 1), Eq(std::unexpected(StringInternerError::kIndexExhausted)));
  EXPECT_THAT(interner.find("failed").has_value(), Eq(false));
  EXPECT_THAT(interner, IsEmpty());
  EXPECT_THAT(interner.insert("retry", 2), Eq(Inserted{0, true}));
  EXPECT_THAT(interner.insert("full", 3), Eq(std::unexpected(StringInternerError::kEntryStorageExhausted)));
  EXPECT_THAT(interner.insert("retry", 99), Eq(Inserted{0, false}));
  EXPECT_THAT(interner, ElementsAre(Pair("retry", 2)));
}

TEST_F(StringInternerTest, OwningAbseilNodeMapPreservesShortAndLongViewsThroughGrowth) {
  using Hash = absl::DefaultHashContainerHash<std::string_view>;
  ExpectDefaultIndexStability<
      StringInternerSet<std::uint64_t, Hash, absl::node_hash_map<std::string, std::uint64_t>>>();
}

TEST_F(StringInternerTest, OwningAbseilNodeSetPreservesShortAndLongViewsThroughGrowth) {
  using Id = StringIdU32<>;
  using Hash = absl::DefaultHashContainerHash<std::string_view>;
  using Index = absl::node_hash_set<std::string>;
  static_assert(kHasStableKeyAddresses<Index>);
  static_assert(SupportsInternerStorage<Index, void>);
  static_assert(SupportsSetStorage<Index, void>);
  static_assert(SupportsMapStorage<Index, void>);
  ExpectDefaultIndexStability<StringInternerSet<Id, Hash, Index>>();
  ExpectMapGrowth<StringInternerMap<Id, std::uint32_t, Hash, Index>>();
}

TEST_F(StringInternerTest, StandardUnorderedMapPreservesBorrowedViewsThroughGrowth) {
  using Id = StringIdU32<>;
  using Hash = std::hash<std::string_view>;
  ExpectDefaultIndexStability<StringInternerSet<Id, Hash, std::unordered_map<std::string_view, Id, Hash>>>();
}

TEST_F(StringInternerTest, StandardUnorderedMapPreservesOwnedViewsThroughGrowth) {
  using Id = StringIdU32<>;
  using Hash = absl::DefaultHashContainerHash<std::string_view>;
  using Map = std::unordered_map<std::string, Id, Hash, std::equal_to<>>;
  ExpectDefaultIndexStability<StringInternerSet<Id, Hash, Map>>();
}

TEST_F(StringInternerTest, StandardOrderedMapOwnsStringsWithoutAHashFunction) {
  using Id = StringIdU32<>;
  using Map = std::map<std::string, Id, std::less<>>;
  ExpectDefaultIndexStability<StringInternerSet<Id, void, Map>>();
}

TEST_F(StringInternerTest, ConstStringKeysOwnTheirCharacterStorage) {
  using Id = StringIdU32<>;
  using Map = std::map<const std::string, Id, std::less<>>;
  static_assert(StringInternerStringContainer<Map>);
  ExpectDefaultIndexStability<StringInternerSet<Id, void, Map>>();
}

TEST_F(StringInternerTest, ConstStringViewKeysBorrowSeparateCharacterStorage) {
  using Id = StringIdU32<>;
  using Map = std::map<const std::string_view, Id, std::less<>>;
  static_assert(StringInternerStringContainer<Map>);
  ExpectDefaultIndexStability<StringInternerSet<Id, void, Map>>();
}

TEST_F(StringInternerTest, KeyOnlyOrderedSetOwnsStrings) {
  using Id = StringIdU32<>;
  using Set = std::set<std::string, std::less<>>;
  ExpectDefaultIndexStability<StringInternerSet<Id, void, Set>>();
}

TEST_F(StringInternerTest, KeyOnlyUnorderedSetBorrowsArenaStorage) {
  using Id = StringIdU32<>;
  using Set = std::unordered_set<std::string_view>;
  ExpectDefaultIndexStability<StringInternerSet<Id, void, Set>>();
}

TEST_F(StringInternerTest, KeyOnlyUnorderedSetOwnsStrings) {
  using Id = StringIdU32<>;
  using Hash = absl::DefaultHashContainerHash<std::string_view>;
  using Set = std::unordered_set<std::string, Hash, std::equal_to<>>;
  ExpectDefaultIndexStability<StringInternerSet<Id, Hash, Set>>();
}

TEST_F(StringInternerTest, KeyOnlyAbseilSetBorrowsArenaStorage) {
  using Id = StringIdU32<>;
  using Set = absl::flat_hash_set<std::string_view>;
  ExpectDefaultIndexStability<StringInternerSet<Id, void, Set>>();
}

TEST_F(StringInternerTest, KeyOnlySetsRespectCapturedParentCutoffs) {
  using Id = StringIdU32<>;
  using Set = std::set<std::string, std::less<>>;
  using Interner = StringInternerSet<Id, void, Set>;
  using Inserted = std::pair<Id, bool>;
  Interner root;
  ASSERT_THAT(root.insert("root"), Eq(Inserted{Id{0}, true}));
  Interner child(&root);
  ASSERT_THAT(child.insert("child"), Eq(Inserted{Id{1}, true}));
  Interner grandchild(&child);
  ASSERT_THAT(root.insert("late-root"), Eq(Inserted{Id{1}, true}));
  ASSERT_THAT(child.insert("late-child"), Eq(Inserted{Id{2}, true}));
  EXPECT_THAT(grandchild.find("late-root"), Eq(std::nullopt));
  EXPECT_THAT(grandchild.rfind("late-child"), Eq(std::nullopt));
  EXPECT_THAT(grandchild.insert("root"), Eq(Inserted{Id{0}, false}));
  EXPECT_THAT(grandchild.insert("child"), Eq(Inserted{Id{1}, false}));
  EXPECT_THAT(grandchild.insert("late-root"), Eq(Inserted{Id{2}, true}));
  EXPECT_THAT(grandchild.find("late-root"), Optional(Id{2}));
  EXPECT_THAT(grandchild.rfind("root"), Optional(Id{0}));
  EXPECT_THAT(grandchild, ElementsAre("root", "child", "late-root"));
}

template<typename Interner>
void ExpectStatefulHashAndCollisions() {
  using Id = Interner::id_type;
  using Inserted = std::pair<Id, bool>;
  std::size_t calls = 0;
  Interner interner(nullptr, 0, CollidingHash{.calls = &calls, .value = 7});
  EXPECT_THAT(interner.insert("first"), Eq(Inserted{Id{0}, true}));
  EXPECT_THAT(interner.insert("second"), Eq(Inserted{Id{1}, true}));
  EXPECT_THAT(interner.insert("first"), Eq(Inserted{Id{0}, false}));
  const auto calls_before_lookup = calls;
  EXPECT_THAT(interner.find("second"), Optional(Id{1}));
  EXPECT_THAT(interner.rfind("missing"), Eq(std::nullopt));
  EXPECT_THAT(calls, Gt(calls_before_lookup));
  EXPECT_THAT(interner, ElementsAre("first", "second"));
}

TEST_F(StringInternerTest, SetWrapperForwardsStatefulHashAndResolvesCollisions) {
  ExpectStatefulHashAndCollisions<StringInternerSet<StringIdU32<>, CollidingHash>>();
}

TEST_F(StringInternerTest, MapWrapperForwardsStatefulHashAndResolvesCollisions) {
  ExpectStatefulHashAndCollisions<StringInternerSet<StringIdU32<>, CollidingHash>>();
}

TEST_F(StringInternerTest, ChangingOnlyTheFirstTemplateArgumentSelectsTheIdType) {
  StringInternerSet<std::int16_t> interner;
  using Inserted = std::pair<std::int16_t, bool>;
  EXPECT_THAT(interner.insert("first"), Eq(Inserted{0, true}));
  EXPECT_THAT(interner.insert("first"), Eq(Inserted{0, false}));
  EXPECT_THAT(interner.find("first"), Optional(std::int16_t{0}));
  EXPECT_THAT(interner.get(0), Optional(Eq("first")));
}

TEST_F(StringInternerTest, ContainerContractAcceptsOrdinaryBorrowingAndOwningMaps) {
  using Borrowing = FixedStringContainer<StringIdU32<>, 2>;
  using Owning = FixedOwningStringContainer<StringIdU32<>, 2>;
  static_assert(StringInternerStringContainer<Borrowing>);
  static_assert(StringInternerStringContainer<Owning>);
}

TEST_F(StringInternerTest, StringKeyValidationDoesNotConstrainMappedValues) {
  using Map = std::map<std::string, std::vector<int>, std::less<>>;
  static_assert(StringInternerStringContainer<Map>);
  static_assert(!StringInternerIdIndex<Map, StringIdU32<>>);
  static_assert(StringInternerIdIndex<std::map<std::string_view, StringIdU32<>>, StringIdU32<>>);
  static_assert(!StringInternerStringContainer<std::map<int, std::string>>);
  static_assert(!StringInternerStringContainer<int>);
  static_assert(!StringInternerStringContainer<std::vector<std::string>>);
}

TEST_F(StringInternerTest, ContainerTraitsDistinguishMapKeysFromSetValues) {
  using Map = std::map<std::string, int, std::less<>>;
  using Set = std::set<std::string, std::less<>>;
  static_assert(StringInternerContainerTraits<Map>::kIsMap);
  static_assert(!StringInternerContainerTraits<Set>::kIsMap);
  static_assert(std::same_as<StringInternerKey<Map>, std::string>);
  static_assert(std::same_as<StringInternerKey<Set>, std::string>);
  static_assert(StringInternerStringContainer<Map>);
  static_assert(StringInternerStringContainer<Set>);
  static_assert(StringInternerIdIndex<Set, StringIdU32<>>);
}

TEST_F(StringInternerTest, StringKeyValidationRejectsMutableKeys) {
  static_assert(!StringInternerStringContainer<MutableStringKeys>);
}

TEST_F(StringInternerTest, StorageSelectionFollowsKeyOwnership) {
  using BorrowingMap = std::map<const std::string_view, StringIdU32<>>;
  using OwningMap = std::map<const std::string, StringIdU32<>, std::less<>>;
  using BorrowingSet = std::set<std::string_view>;
  using OwningSet = std::set<std::string, std::less<>>;
  static_assert(SupportsInternerStorage<BorrowingMap, ArenaStringStorage<>>);
  static_assert(SupportsInternerStorage<BorrowingSet, ArenaStringStorage<>>);
  static_assert(!SupportsInternerStorage<BorrowingMap, void>);
  static_assert(!SupportsInternerStorage<BorrowingSet, void>);
  static_assert(SupportsInternerStorage<OwningMap, void>);
  static_assert(SupportsInternerStorage<OwningSet, void>);
  static_assert(!SupportsInternerStorage<OwningMap, ArenaStringStorage<>>);
  static_assert(!SupportsInternerStorage<OwningSet, ArenaStringStorage<>>);
}

TEST_F(StringInternerTest, CustomOwningIndexesRequireAnExternalStabilityTrait) {
  using CustomIndex = FixedOwningStringContainer<StringIdU32<>, 2>;
  static_assert(kHasStableKeyAddresses<CustomIndex>);
  static_assert(SupportsInternerStorage<CustomIndex, void>);
  static_assert(MarkerOnlyStringMap::kStringInternerStableKeyAddresses);
  static_assert(!kHasStableKeyAddresses<MarkerOnlyStringMap>);
  static_assert(!SupportsInternerStorage<MarkerOnlyStringMap, void>);
}

TEST_F(StringInternerTest, OwningUnstableKeyContainersAreRejected) {
  using OwningMap = absl::flat_hash_map<std::string, StringIdU32<>>;
  using BorrowingMap = absl::flat_hash_map<std::string_view, StringIdU32<>>;
  static_assert(!SupportsInternerStorage<OwningMap, void>);
  static_assert(!SupportsInternerStorage<absl::flat_hash_set<std::string>, void>);
  static_assert(SupportsInternerStorage<BorrowingMap, ArenaStringStorage<>>);
  static_assert(SupportsInternerStorage<absl::flat_hash_set<std::string_view>, ArenaStringStorage<>>);
}

TEST_F(StringInternerTest, WrappersAcceptBothIndexKindsAndEnforceStorageOwnership) {
  using Entry = StringIdU32<>;
  using BorrowingMap = std::map<const std::string_view, Entry>;
  using OwningMap = std::map<const std::string, Entry, std::less<>>;
  using BorrowingSet = std::set<std::string_view>;
  using OwningSet = std::set<std::string, std::less<>>;
  static_assert(SupportsMapStorage<BorrowingMap, ArenaStringStorage<>>);
  static_assert(SupportsMapStorage<OwningMap, void>);
  static_assert(!SupportsMapStorage<BorrowingMap, void>);
  static_assert(!SupportsMapStorage<OwningMap, ArenaStringStorage<>>);
  static_assert(SupportsMapStorage<BorrowingSet, ArenaStringStorage<>>);
  static_assert(SupportsMapStorage<OwningSet, void>);
  static_assert(SupportsSetStorage<BorrowingSet, ArenaStringStorage<>>);
  static_assert(SupportsSetStorage<OwningSet, void>);
  static_assert(!SupportsSetStorage<BorrowingSet, void>);
  static_assert(!SupportsSetStorage<OwningSet, ArenaStringStorage<>>);
  static_assert(SupportsSetStorage<std::map<std::string_view, StringIdU32<>>, ArenaStringStorage<>>);
  static_assert(SupportsSetStorage<std::map<std::string, StringIdU32<>, std::less<>>, void>);
  static_assert(!SupportsMapStorage<absl::flat_hash_map<std::string, Entry>, void>);
  static_assert(!SupportsSetStorage<absl::flat_hash_set<std::string>, void>);
}

TEST_F(StringInternerTest, InsertPublishesTheLocalIndexAndReusesIdsAtCapacity) {
  TestStringInterner<StringId<>, 5, 1> interner;
  using Inserted = std::pair<StringId<>, bool>;
  EXPECT_THAT(interner.insert("first"), Eq(Inserted{StringId<>(0), true}));
  EXPECT_THAT(interner.find("first"), Optional(StringId<>(0)));
  EXPECT_THAT(interner.rfind("first"), Optional(StringId<>(0)));
  EXPECT_THAT(interner.get(StringId<>(0)), Optional(Eq("first")));
  EXPECT_THAT(interner.insert("first"), Eq(Inserted{StringId<>(0), false}));
  EXPECT_THAT(interner, ElementsAre("first"));
}

TEST_F(StringInternerTest, InsertReusesInheritedIdsAndAddsNewStringsOnlyLocally) {
  TestStringInterner<> parent;
  using Inserted = std::pair<StringId<>, bool>;
  EXPECT_THAT(parent.insert("shared"), Eq(Inserted{StringId<>(0), true}));
  TestStringInterner<> child(&parent);
  EXPECT_THAT(child.insert("shared"), Eq(Inserted{StringId<>(0), false}));
  EXPECT_THAT(child.local_size(), Eq(0));
  EXPECT_THAT(child.insert("local"), Eq(Inserted{StringId<>(1), true}));
  EXPECT_THAT(child.insert("local"), Eq(Inserted{StringId<>(1), false}));
  EXPECT_THAT(child.find("local"), Optional(StringId<>(1)));
  EXPECT_THAT(child, ElementsAre("shared", "local"));
  EXPECT_THAT(parent, ElementsAre("shared"));
  EXPECT_THAT(parent.find("local"), Eq(std::nullopt));
}

TEST_F(StringInternerTest, OwningContainerKeepsViewsStableAndReusesIdsWithoutSeparateStorage) {
  using Id = StringIdU32<>;
  using Interner = StringInternerSet<Id, void, FixedOwningStringContainer<Id, 3>>;
  using Inserted = std::pair<Id, bool>;
  Interner interner;
  std::string input("a\0b", 3);
  EXPECT_THAT(interner.insert(input), Eq(Inserted{Id(0), true}));
  const auto original = interner.get_string(Id(0)).value_or(std::string_view{});
  const auto position = interner.begin();
  input.assign("changed");
  EXPECT_THAT(original, Eq(std::string_view("a\0b", 3)));
  EXPECT_THAT(interner.insert(""), Eq(Inserted{Id(1), true}));
  EXPECT_THAT(interner.insert(std::string(256, 'x')), Eq(Inserted{Id(2), true}));
  EXPECT_THAT(*position, Eq(original));
  EXPECT_THAT(interner.find(original), Optional(Id(0)));
  EXPECT_THAT(interner.rfind(original), Optional(Id(0)));
  EXPECT_THAT(interner.insert(original), Eq(Inserted{Id(0), false}));
  EXPECT_THAT(interner.get(Id(1)), Optional(Eq("")));
  EXPECT_THAT(interner.get(Id(2)), Optional(Eq(std::string(256, 'x'))));
  EXPECT_THAT(interner.insert("full"), Eq(std::unexpected(StringInternerError::kIndexExhausted)));
  EXPECT_THAT(interner.find("full"), Eq(std::nullopt));
  EXPECT_THAT(interner.get(Id(3)), Eq(std::nullopt));
}

TEST_F(StringInternerTest, OwningContainerFailureReusesTheUnpublishedAppendOnlyEntry) {
  using Id = StringIdU32<>;
  using Interner =
      StringInternerSet<Id, void, FixedOwningStringContainer<Id, 1, true>, void, AppendOnlyEntries<const std::string*>>;
  using Inserted = std::pair<Id, bool>;
  Interner interner;
  EXPECT_THAT(interner.insert("failed"), Eq(std::unexpected(StringInternerError::kIndexExhausted)));
  EXPECT_THAT(interner, IsEmpty());
  EXPECT_THAT(interner.find("failed"), Eq(std::nullopt));
  EXPECT_THAT(interner.insert("retry"), Eq(Inserted{Id(0), true}));
  EXPECT_THAT(interner.find("retry"), Optional(Id(0)));
  EXPECT_THAT(interner, ElementsAre("retry"));
}

TEST_F(StringInternerTest, EntryExhaustionDoesNotPublishIntoTheOwningContainer) {
  using Id = StringIdU32<>;
  using Entries = mbo::container::SegmentedVector<
      const std::string*,
      mbo::container::SegmentedOptions{.segment_size = 1, .segment_capacity = 1, .segment_reservation = 1}>;
  using Interner = StringInternerSet<Id, void, FixedOwningStringContainer<Id, 2>, void, Entries>;
  using Inserted = std::pair<Id, bool>;
  Interner interner;
  EXPECT_THAT(interner.insert("first"), Eq(Inserted{Id(0), true}));
  EXPECT_THAT(interner.insert("second"), Eq(std::unexpected(StringInternerError::kEntryStorageExhausted)));
  EXPECT_THAT(interner.find("second"), Eq(std::nullopt));
  EXPECT_THAT(interner.insert("first"), Eq(Inserted{Id(0), false}));
  EXPECT_THAT(interner, ElementsAre("first"));
}

TEST_F(StringInternerTest, OwningContainerInheritsOnlyTheCapturedParentPrefix) {
  using Id = StringIdU32<>;
  using Interner = StringInternerSet<Id, void, FixedOwningStringContainer<Id, 3>>;
  using Inserted = std::pair<Id, bool>;
  Interner parent;
  EXPECT_THAT(parent.insert("shared"), Eq(Inserted{Id(0), true}));
  Interner child(&parent);
  EXPECT_THAT(child.insert("shared"), Eq(Inserted{Id(0), false}));
  EXPECT_THAT(child.local_size(), Eq(0));
  EXPECT_THAT(child.insert("local"), Eq(Inserted{Id(1), true}));
  EXPECT_THAT(parent.insert("later"), Eq(Inserted{Id(1), true}));
  EXPECT_THAT(child.find("later"), Eq(std::nullopt));
  EXPECT_THAT(child.insert("later"), Eq(Inserted{Id(2), true}));
  EXPECT_THAT(child.find("later"), Optional(Id(2)));
  EXPECT_THAT(child.rfind("shared"), Optional(Id(0)));
  EXPECT_THAT(child, ElementsAre("shared", "local", "later"));
  EXPECT_THAT(parent, ElementsAre("shared", "later"));
  EXPECT_THAT(parent.find("local"), Eq(std::nullopt));
}

TEST_F(StringInternerTest, CharacterAndEntryExhaustionLeavePublishedStringsUnchanged) {
  TestStringInterner<StringId<>, 0> bounded;
  EXPECT_THAT(bounded.insert("x"), Eq(std::unexpected(StringInternerError::kCharacterStorageExhausted)));
  EXPECT_THAT(bounded.size(), Eq(0));
  EXPECT_THAT(bounded.insert("").has_value(), Eq(true));
  EXPECT_THAT(bounded.get(StringId<>(0)), Optional(std::string_view{}));

  using Entries = mbo::container::SegmentedVector<
      std::string_view,
      mbo::container::SegmentedOptions{.segment_size = 1, .segment_capacity = 1, .segment_reservation = 1}>;
  TestStringInterner<StringId<>, 64, 8, Entries> one;
  EXPECT_THAT(one.insert("first").has_value(), Eq(true));
  const auto original = one.get_string(StringId<>(0)).value_or(std::string_view{});
  EXPECT_THAT(one.insert("second"), Eq(std::unexpected(StringInternerError::kEntryStorageExhausted)));
  EXPECT_THAT(one.size(), Eq(1));
  EXPECT_THAT(original, Eq("first"));
  EXPECT_THAT(one.find("second"), Eq(std::nullopt));
}

TEST_F(StringInternerTest, EmbeddedNulsAndExistingIteratorsSurviveInputMutationAndAppend) {
  TestStringInterner<> interner;
  std::string text("a\0b", 3);
  EXPECT_THAT(interner.insert(text).has_value(), Eq(true));
  auto position = interner.begin();
  const auto original = *position;
  text.assign("changed");
  EXPECT_THAT(interner.insert("a").has_value(), Eq(true));
  EXPECT_THAT(original, Eq(std::string_view("a\0b", 3)));
  EXPECT_THAT(*position, Eq(original));
  EXPECT_THAT(interner.find(original), Optional(StringId<>(0)));
  EXPECT_THAT(interner.rfind("a"), Optional(StringId<>(1)));
  ++position;
  EXPECT_THAT(*position, Eq("a"));
  ++position;
  EXPECT_THAT(position == interner.end(), Eq(true));
}

TEST_F(StringInternerTest, FailedStringContainerInsertionReusesTheUnpublishedAppendOnlyEntry) {
  using Map = BoundedStringMap<std::string_view, StringId<>, 1, true>;
  using Interner = StringInternerSet<StringId<>, void, Map, FixedStringStorage<11>, AppendOnlyEntries<>>;
  Interner interner;
  EXPECT_THAT(interner.insert("failed"), Eq(std::unexpected(StringInternerError::kIndexExhausted)));
  EXPECT_THAT(interner.size(), Eq(0));
  EXPECT_THAT(interner.find("failed"), Eq(std::nullopt));
  using Inserted = std::pair<StringId<>, bool>;
  EXPECT_THAT(interner.insert("retry"), Eq(Inserted{StringId<>(0), true}));
  EXPECT_THAT(interner.find("retry"), Optional(StringId<>(0)));
  EXPECT_THAT(interner.get(StringId<>(0)), Optional(std::string_view("retry")));
}

TEST_F(StringInternerTest, FailedIndexInsertionRetainsBytesWithoutPublishingAnId) {
  using Map = BoundedStringMap<std::string_view, StringId<>, 1, true>;
  using Interner = StringInternerSet<StringId<>, void, Map, FixedStringStorage<6>, AppendOnlyEntries<>>;
  using Inserted = std::pair<StringId<>, bool>;
  Interner interner;
  EXPECT_THAT(interner.insert("failed"), Eq(std::unexpected(StringInternerError::kIndexExhausted)));
  EXPECT_THAT(interner.insert("retry"), Eq(std::unexpected(StringInternerError::kCharacterStorageExhausted)));
  EXPECT_THAT(interner, IsEmpty());
  EXPECT_THAT(interner.find("failed"), Eq(std::nullopt));
  EXPECT_THAT(interner.find("retry"), Eq(std::nullopt));
  EXPECT_THAT(interner.get(StringId<>(0)), Eq(std::nullopt));
  EXPECT_THAT(interner.insert(""), Eq(Inserted{StringId<>(0), true}));
  EXPECT_THAT(interner, ElementsAre(""));
}

TEST_F(StringInternerTest, IndexExhaustionPreservesPublishedEntriesAndDuplicateLookup) {
  TestStringInterner<StringId<>, 64, 1> interner;
  using Inserted = std::pair<StringId<>, bool>;
  EXPECT_THAT(interner.insert("first"), Eq(Inserted{StringId<>(0), true}));
  EXPECT_THAT(interner.insert("second"), Eq(std::unexpected(StringInternerError::kIndexExhausted)));
  EXPECT_THAT(interner.find("second"), Eq(std::nullopt));
  EXPECT_THAT(interner.get(StringId<>(1)), Eq(std::nullopt));
  EXPECT_THAT(interner.insert("first"), Eq(Inserted{StringId<>(0), false}));
  EXPECT_THAT(interner, ElementsAre("first"));
}

TEST_F(StringInternerTest, EightBitIdsReserveInvalidValueAndFindDuplicatesAfterExhaustion) {
  TestStringInterner<StringId<std::uint8_t>> interner;
  for (unsigned value = 0; value < StringId<std::uint8_t>::invalid_value; ++value) {
    EXPECT_THAT(interner.insert(std::to_string(value)).has_value(), Eq(true));
  }
  EXPECT_THAT(interner.size(), Eq(255));
  EXPECT_THAT(interner.get(StringId<std::uint8_t>(254)), Optional(std::string_view("254")));
  EXPECT_THAT(interner.get(StringId<std::uint8_t>{}), Eq(std::nullopt));
  EXPECT_THAT(interner.insert("exhausted"), Eq(std::unexpected(StringInternerError::kIdExhausted)));
  EXPECT_THAT(interner.insert("0").has_value(), Eq(true));
  EXPECT_THAT(interner.size(), Eq(255));
}

TEST_F(StringInternerTest, EmptyDeclaredParentRetainsIdentityWithoutExposingLaterValues) {
  TestStringInterner<> root;
  TestStringInterner<> child(&root);
  EXPECT_THAT(child.parent(), Eq(&root));
  EXPECT_THAT(child.first_local_id(), Eq(0));
  EXPECT_THAT(root.insert("later").has_value(), Eq(true));
  EXPECT_THAT(child.find("later"), Eq(std::nullopt));
  EXPECT_THAT(child.rfind("later"), Eq(std::nullopt));
  EXPECT_THAT(child.insert("local").has_value(), Eq(true));
  EXPECT_THAT(child.get(StringId<>(0)), Optional(std::string_view("local")));
}

TEST_F(StringInternerTest, IterationIncludesCapturedAncestorsAndPreservesBranchIdentity) {
  TestStringInterner<> root;
  EXPECT_THAT(root.insert("root").has_value(), Eq(true));
  TestStringInterner<> child(&root);
  EXPECT_THAT(child.insert("child").has_value(), Eq(true));
  TestStringInterner<> grandchild(&child);
  EXPECT_THAT(child.insert("invisible").has_value(), Eq(true));
  EXPECT_THAT(grandchild.insert("grandchild").has_value(), Eq(true));
  EXPECT_THAT(grandchild, ElementsAre("root", "child", "grandchild"));
  auto reverse = grandchild.rbegin();
  EXPECT_THAT(*reverse++, Eq("grandchild"));
  EXPECT_THAT(*reverse++, Eq("child"));
  EXPECT_THAT(*reverse++, Eq("root"));
  EXPECT_THAT(reverse == grandchild.rend(), Eq(true));
  EXPECT_THAT(root.begin() == child.begin(), Eq(false));
  EXPECT_THAT(grandchild.find("invisible"), Eq(std::nullopt));
  EXPECT_THAT(grandchild.rfind("invisible"), Eq(std::nullopt));
}

TEST_F(StringInternerTest, ChildCapturesPrefixAndIgnoresLaterParentInsertions) {
  TestStringInterner<> root;
  EXPECT_THAT(root.insert("root").has_value(), Eq(true));
  TestStringInterner<> child(&root);
  EXPECT_THAT(child.insert("child").has_value(), Eq(true));
  EXPECT_THAT(root.insert("later").has_value(), Eq(true));
  EXPECT_THAT(child.find("later"), Eq(std::nullopt));
  EXPECT_THAT(child.insert("later").has_value(), Eq(true));
  EXPECT_THAT(root.find("later"), Optional(StringId<>(1)));
  EXPECT_THAT(child.find("later"), Optional(StringId<>(2)));
  EXPECT_THAT(child.find("root"), Optional(StringId<>(0)));
  EXPECT_THAT(child.rfind("root"), Optional(StringId<>(0)));
  EXPECT_THAT(child.get(StringId<>(1)), Optional(std::string_view("child")));
  EXPECT_THAT(child.size(), Eq(3));
  EXPECT_THAT(child.local_size(), Eq(2));
}

template<typename Entries>
void ExpectEntryContainerSupport() {
  TestStringInterner<StringId<>, 64, 8, Entries> interner;
  EXPECT_THAT(interner.insert("first").has_value(), Eq(true));
  EXPECT_THAT(interner.insert("second").has_value(), Eq(true));
  EXPECT_THAT(interner, ElementsAre("first", "second"));
  EXPECT_THAT(interner.get(StringId<>(1)), Optional(std::string_view("second")));
}

TEST_F(StringInternerTest, SupportsSegmentedListDequeAndVectorEntryContainers) {
  ExpectEntryContainerSupport<mbo::container::SegmentedVector<std::string_view>>();
  ExpectEntryContainerSupport<std::list<std::string_view>>();
  ExpectEntryContainerSupport<std::deque<std::string_view>>();
  ExpectEntryContainerSupport<std::vector<std::string_view>>();
}

TEST_F(StringInternerTest, SupportsArenaCharacterStorage) {
  using Interner = StringInternerSet<
      StringId<>, void, FixedStringContainer<StringId<>, 8>, ArenaStringStorage<>, std::vector<std::string_view>>;
  Interner interner;
  std::string input = "arena-owned";
  EXPECT_THAT(interner.insert(input).has_value(), Eq(true));
  input.assign("changed");
  EXPECT_THAT(interner.get(StringId<>(0)), Optional(std::string_view("arena-owned")));
}

template<typename Id>
void ExpectNegativeIdsRejectedAcrossParentChain() {
  using Interner = TestStringInterner<Id>;
  using Inserted = std::pair<Id, bool>;
  static_assert(std::unsigned_integral<typename Interner::size_type>);
  static constexpr auto kNegativeIds = std::to_array<Id>({Id{-1}, Id{std::numeric_limits<std::int64_t>::min()}});

  Interner root;
  for (const auto identifier : kNegativeIds) {
    EXPECT_THAT(root.get(identifier), Eq(std::nullopt));
  }
  ASSERT_THAT(root.insert("root"), Eq(Inserted{Id{0}, true}));
  const Interner child(&root);
  Interner grandchild(&child);
  ASSERT_THAT(grandchild.insert("grandchild"), Eq(Inserted{Id{1}, true}));

  for (const auto identifier : kNegativeIds) {
    EXPECT_THAT(root.get(identifier), Eq(std::nullopt));
    EXPECT_THAT(child.get(identifier), Eq(std::nullopt));
    EXPECT_THAT(grandchild.get(identifier), Eq(std::nullopt));
  }
  EXPECT_THAT(grandchild.get(Id{0}), Optional(Eq("root")));
  EXPECT_THAT(grandchild.get(Id{1}), Optional(Eq("grandchild")));
  EXPECT_THAT(grandchild.get(Id{2}), Eq(std::nullopt));
}

TEST_F(StringInternerTest, NegativePlainIdsAreRejectedAcrossParentChain) {
  ExpectNegativeIdsRejectedAcrossParentChain<std::int64_t>();
}

TEST_F(StringInternerTest, NegativeStringIdsAreRejectedAcrossParentChain) {
  ExpectNegativeIdsRejectedAcrossParentChain<StringId<std::int64_t>>();
}

TEST_F(StringInternerTest, SupportsPlainAndStringIdsWithSignedAndUnsignedBacking) {
  TestStringInterner<std::int16_t> plain_signed;
  TestStringInterner<std::uint16_t> plain_unsigned;
  TestStringInterner<StringId<std::int16_t>> string_signed;
  TestStringInterner<StringId<std::uint16_t>> string_unsigned;

  using PlainSignedResult = std::pair<std::int16_t, bool>;
  using PlainUnsignedResult = std::pair<std::uint16_t, bool>;
  using StringSignedResult = std::pair<StringId<std::int16_t>, bool>;
  using StringUnsignedResult = std::pair<StringId<std::uint16_t>, bool>;
  EXPECT_THAT(plain_signed.insert("plain-signed"), Eq(PlainSignedResult{0, true}));
  EXPECT_THAT(plain_unsigned.insert("plain-unsigned"), Eq(PlainUnsignedResult{0, true}));
  EXPECT_THAT(string_signed.insert("string-signed"), Eq(StringSignedResult{StringId<std::int16_t>{0}, true}));
  EXPECT_THAT(string_unsigned.insert("string-unsigned"), Eq(StringUnsignedResult{StringId<std::uint16_t>{0}, true}));
  EXPECT_THAT(plain_signed.get(-1), Eq(std::nullopt));
  EXPECT_THAT(string_signed.get(StringId<std::int16_t>{-1}), Eq(std::nullopt));
}

}  // namespace
}  // namespace mbo::strings
