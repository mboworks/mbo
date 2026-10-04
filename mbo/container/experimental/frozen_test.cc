// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/experimental/frozen_map.h"
#include "mbo/container/experimental/frozen_options.h"
#include "mbo/container/experimental/frozen_set.h"
#include "mbo/hash/hash.h"

namespace mbo::container::experimental {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::FloatEq;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;
using namespace std::string_view_literals;

struct FrozenTest : ::testing::Test {
  template<typename Hash, typename ReferenceHash = Hash>
  static void CheckMboHash() {
    static constexpr auto kBytes = [] {
      std::array<char, 256> bytes{};
      for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes.at(index) = static_cast<char>(index);
      }
      return bytes;
    }();
    static constexpr auto kLengths = std::to_array<std::size_t>({
        0, 1, 3, 4, 7, 8, 9, 10, 15, 16, 17, 32, 48, 63, 64, 127, 128, 129, 255,
    });
    static constexpr auto kEntries = [] {
      std::array<std::pair<std::string_view, std::size_t>, kLengths.size()> entries{};
      for (std::size_t index = 0; index < entries.size(); ++index) {
        entries.at(index) = {std::string_view(kBytes.data(), kLengths.at(index)), index};
      }
      return entries;
    }();
    static constexpr auto kStringKeys = [] {
      std::array<std::string_view, kLengths.size()> keys{};
      for (std::size_t index = 0; index < keys.size(); ++index) {
        keys.at(index) = kEntries.at(index).first;
      }
      return keys;
    }();
    static constexpr auto kHashes = [] {
      std::array<std::uint64_t, kLengths.size()> hashes{};
      for (std::size_t index = 0; index < hashes.size(); ++index) {
        hashes.at(index) = ReferenceHash{}(kStringKeys.at(index));
      }
      return hashes;
    }();
    static constexpr FrozenMap<std::string_view, std::size_t, kLengths.size(), Hash> kHashMap(kEntries);
    static constexpr FrozenSet<std::string_view, kLengths.size(), Hash> kHashSet(kStringKeys);
    static_assert(kHashMap.at(kStringKeys.back()) == kLengths.size() - 1);
    static_assert(kHashSet.contains(kStringKeys.back()));
    for (std::size_t index = 0; index < kStringKeys.size(); ++index) {
      std::string key(kStringKeys.at(index));
      EXPECT_THAT(Hash{}(key), kHashes.at(index));
      EXPECT_THAT(kHashMap.at(key), index);
      EXPECT_THAT(kHashSet.contains(key), IsTrue());
      EXPECT_THAT(kHashMap.equal_range(key).first->second, index);
      if (key.empty()) {
        key.push_back(static_cast<char>(0xff));
      } else {
        key.front() = static_cast<char>(0xff);
      }
      EXPECT_THAT(kHashMap.contains(key), IsFalse());
      EXPECT_THAT(kHashSet.contains(key), IsFalse());
    }
  }
};

TEST_F(FrozenTest, MumboHashMatchesConstantEvaluation) {
  CheckMboHash<mbo::hash::Hasher<mbo::hash::mumbo::Algorithm>>();
}

TEST_F(FrozenTest, FamboHashMatchesConstantEvaluation) {
  CheckMboHash<mbo::hash::Hasher<mbo::hash::fambo::Algorithm>>();
}

TEST_F(FrozenTest, DumboHashMatchesConstantEvaluation) {
  CheckMboHash<mbo::hash::Hasher<mbo::hash::dumbo::Algorithm>>();
}

constexpr auto kKeys = std::to_array<std::string_view>({"", "-name", "-n", "a\0b"sv, "\x80\xff"sv, "-type", "!"});
constexpr FrozenSet kSet(kKeys);
constexpr auto kPairs = std::to_array<std::pair<std::string_view, int>>({{"-name", 1}, {"-n", 1}, {"-type", 2}});
constexpr FrozenMap kMap(kPairs);
static_assert(kSet.contains("a\0b"sv));
static_assert(kMap.at("-name") == 1);
static_assert(kMap.lookup("-n").has_value());
static_assert(!kMap.lookup("unknown"));
static_assert(std::forward_iterator<decltype(kMap)::iterator>);
static_assert(std::forward_iterator<decltype(kMap)::local_iterator>);
static_assert(std::ranges::forward_range<decltype(kSet)>);
static_assert(std::ranges::sized_range<decltype(kSet)>);
static_assert(!std::is_assignable_v<decltype(*kMap.begin()), std::pair<std::string_view, int>>);
static_assert(!std::is_assignable_v<decltype((kMap.begin()->second)), int>);
static_assert(!std::is_assignable_v<decltype(*kSet.begin()), std::string_view>);
static_assert(!std::is_copy_assignable_v<decltype(kSet)>);
static_assert(std::same_as<decltype(kMap)::value_type, std::pair<const std::string_view, int>>);

TEST_F(FrozenTest, DefaultStringHashUsesFambo) {
  using MapHash = decltype(kMap)::hasher;
  using SetHash = decltype(kSet)::hasher;
  static_assert(std::same_as<MapHash, SetHash>);
  CheckMboHash<MapHash, mbo::hash::Hasher<mbo::hash::fambo::Algorithm>>();
}

TEST_F(FrozenTest, EmptyTablesAndZeroCapacity) {
  static constexpr FrozenMap<int, int, 0> kEmptyMap;
  static constexpr FrozenSet<int, 0> kEmptySet;
  static constexpr FrozenSet<int, 8> kReserved;
  static_assert(kEmptySet.empty() && kEmptyMap.empty() && kReserved.empty());
  EXPECT_THAT(kEmptyMap, IsEmpty());
  EXPECT_THAT(kEmptySet.find(1), kEmptySet.end());
  EXPECT_THAT(kEmptyMap.lookup(1), Eq(std::nullopt));
  EXPECT_THAT(kEmptySet.count(1), 0);
}

TEST_F(FrozenTest, EmptyTableBucketsAndRanges) {
  static constexpr FrozenSet<int, 0> kEmptySet;
  static constexpr FrozenSet<int, 8> kReserved;
  EXPECT_THAT(kEmptySet.bucket_count(), 0);
  EXPECT_THAT(kEmptySet.load_factor(), FloatEq(0));
  EXPECT_THAT(kEmptySet.equal_range(1), Pair(kEmptySet.end(), kEmptySet.end()));
  EXPECT_THAT(kReserved.bucket_size(0), 0);
  EXPECT_THAT(kReserved.begin(0), kReserved.end(0));
}

TEST_F(FrozenTest, SingletonChecksCandidateEquality) {
  static constexpr FrozenSet<int, FrozenOptions{.capacity = 1, .slots = 1}> kSingle({42});
  EXPECT_THAT(kSingle, ElementsAre(42));
  EXPECT_THAT(kSingle.lookup(42), Optional(Eq(42)));
  EXPECT_THAT(kSingle.lookup(7), Eq(std::nullopt));
  EXPECT_THAT(kSingle.bucket(42), kSingle.bucket(7));
  EXPECT_THAT(kSingle.load_factor(), FloatEq(1));
  EXPECT_THAT(kSingle.max_load_factor(), FloatEq(1));
}

TEST_F(FrozenTest, AliasesAndIndependentModes) {
  static constexpr FrozenMap<std::string_view, int, 1> kOtherMode({{"-n", 7}});
  EXPECT_THAT(kMap.at("-name"), 1);
  EXPECT_THAT(kMap.at("-n"), 1);
  EXPECT_THAT(kOtherMode.at("-n"), 7);
  EXPECT_THAT(kOtherMode.contains("-name"), IsFalse());
  EXPECT_THAT(kMap, UnorderedElementsAre(Pair("-name", 1), Pair("-n", 1), Pair("-type", 2)));
  EXPECT_THAT(kMap.index_of("-type"), 2);
  EXPECT_THAT(kMap.at_index(2), Pair("-type", 2));
  EXPECT_THAT(kMap.index_of("miss"), decltype(kMap)::npos);
  EXPECT_THAT(kMap.count("-type"), 1);
  EXPECT_THAT(kMap.count("miss"), 0);
}

TEST_F(FrozenTest, ByteStringsAndAdversarialMisses) {
  for (const auto key : kKeys) {
    const std::string runtime(key);
    EXPECT_THAT(kSet.contains(runtime), IsTrue());
    EXPECT_THAT(kSet.lookup(runtime), Optional(Eq(key)));
  }
  static constexpr auto kMisses = std::to_array<std::string_view>({"a", "a\0c"sv, "-Name", "-names", "\xff\x80"sv});
  for (const auto key : kMisses) {
    EXPECT_THAT(kSet.contains(key), IsFalse());
  }
  EXPECT_THAT(kSet.contains(std::string(100'000, 'x')), IsFalse());
}

TEST_F(FrozenTest, EveryByteCanBeQueried) {
  for (int byte = 0; byte < 256; ++byte) {
    const std::string key(1, static_cast<char>(byte));
    EXPECT_THAT(kSet.contains(key), key == "!");
  }
}

TEST_F(FrozenTest, EveryKeyHasItsOwnOccupiedSlot) {
  std::array<bool, kSet.bucket_count()> occupied{};
  for (const auto key : kSet) {
    const auto bucket = kSet.bucket(key);
    EXPECT_THAT(occupied.at(bucket), IsFalse());
    occupied.at(bucket) = true;
    EXPECT_THAT(*kSet.cbegin(bucket), key);
    auto iterator = kSet.begin(bucket);
    EXPECT_THAT(*iterator++, key);
    EXPECT_THAT(iterator, kSet.cend(bucket));
  }
}

TEST_F(FrozenTest, LocalIterationVisitsExactlyTheElements) {
  std::size_t count = 0;
  for (std::size_t bucket = 0; bucket < kSet.bucket_count(); ++bucket) {
    count += kSet.bucket_size(bucket);
    EXPECT_THAT(std::distance(kSet.begin(bucket), kSet.end(bucket)), kSet.bucket_size(bucket));
  }
  EXPECT_THAT(count, kSet.size());
  EXPECT_THAT(kSet.max_bucket_count(), kSet.bucket_count());
}

TEST_F(FrozenTest, IdenticalDuplicatesCollapse) {
  static constexpr FrozenSet<int, 4> kRepeated({1, 1, 2, 1});
  static constexpr FrozenMap<int, int, 4> kRepeatedMap({{1, 4}, {1, 4}, {2, 5}});
  EXPECT_THAT(kRepeated, UnorderedElementsAre(1, 2));
  EXPECT_THAT(kRepeatedMap, UnorderedElementsAre(Pair(1, 4), Pair(2, 5)));
}

struct NonDefault {
  int value;
  NonDefault() = delete;

  constexpr explicit NonDefault(int number) : value(number) {}

  constexpr bool operator==(const NonDefault&) const = default;
};

struct CustomHash {
  int salt;

  constexpr std::uint64_t operator()(const NonDefault& value) const { return FrozenHash<int>{}(value.value + salt); }
};

TEST_F(FrozenTest, GenericKeysAndNonDefaultMappedValues) {
  static constexpr auto kCustomPairs =
      std::to_array<std::pair<NonDefault, NonDefault>>({{NonDefault(2), NonDefault(7)}});
  static constexpr FrozenMap<NonDefault, NonDefault, 2, CustomHash> kCustom(kCustomPairs, CustomHash{3});
  EXPECT_THAT(kCustom.lookup(NonDefault(2))->value, 7);
  EXPECT_THAT(kCustom.hash_function().salt, 3);
  EXPECT_THAT(kCustom.key_eq()(NonDefault(2), NonDefault(2)), IsTrue());
  enum class Key { kFirst, kSecond };
  static constexpr FrozenSet<Key, 2> kEnums({Key::kFirst, Key::kSecond});
  EXPECT_THAT(kEnums.contains(Key::kSecond), IsTrue());
}

TEST_F(FrozenTest, ConstructionFactoriesAndCopies) {
  static constexpr auto kMade = MakeFrozenMap(kPairs);
  static constexpr auto kConverted = ToFrozenSet(kKeys);
  static constexpr FrozenMap<std::string_view, int, 4> kIterators(kPairs.begin(), kPairs.end());
  static constexpr FrozenSet<std::string_view, 9> kRange(std::from_range, kKeys);
  auto copy = kMade;
  const auto moved = std::move(copy);
  EXPECT_THAT(moved, kMap);
  EXPECT_THAT(kConverted, kSet);
  EXPECT_THAT(kIterators.at("-type"), 2);
  EXPECT_THAT(kRange, SizeIs(kKeys.size()));
  EXPECT_THAT(kRange.capacity(), 9);
  EXPECT_THAT(kRange.max_size(), 9);
  EXPECT_THAT(ToFrozenMap(kPairs), kMap);
  EXPECT_THAT(MakeFrozenSet(kKeys), kSet);
}

TEST_F(FrozenTest, RangesAndEqualityIgnoreInsertionOrder) {
  static constexpr auto kOrder = std::to_array<int>({3, 1, 2});
  static constexpr auto kReversed = std::to_array<int>({2, 1, 3});
  static constexpr FrozenSet kFirst(kOrder);
  static constexpr FrozenSet kSecond(kReversed);
  static constexpr FrozenSet<int, 3> kDifferent({1, 2, 4});
  static constexpr std::array<int, 0> kEmpty{};
  EXPECT_THAT(kFirst, kSecond);
  EXPECT_THAT(kFirst == kDifferent, IsFalse());
  EXPECT_THAT(kFirst.contains_all(kReversed), IsTrue());
  EXPECT_THAT(kFirst.contains_any(kReversed), IsTrue());
  EXPECT_THAT(kFirst.contains_all(kEmpty), IsTrue());
  EXPECT_THAT(kFirst.contains_any(kEmpty), IsFalse());
  EXPECT_THAT(kFirst.cbegin(), kFirst.begin());
  EXPECT_THAT(kFirst.cend(), kFirst.end());
  const auto [first, last] = kFirst.equal_range(1);
  EXPECT_THAT(std::ranges::subrange(first, last), ElementsAre(1));
}

TEST_F(FrozenTest, ManyKeysInMinimalAndSparseTables) {
  static constexpr auto kMany = [] {
    std::array<int, 128> keys{};
    for (std::size_t index = 0; index < keys.size(); ++index) {
      keys.at(index) = static_cast<int>(index * 17);
    }
    return keys;
  }();
  static constexpr FrozenSet<int, FrozenOptions{.capacity = kMany.size(), .slots = kMany.size()}> kMinimal(kMany);
  static constexpr FrozenSet kSparse(kMany);
  for (const int key : kMany) {
    EXPECT_THAT(kMinimal.contains(key), IsTrue());
    EXPECT_THAT(kSparse.contains(key), IsTrue());
    EXPECT_THAT(kMinimal.contains(key + 1), IsFalse());
    EXPECT_THAT(kSparse.contains(key + 1), IsFalse());
  }
}

TEST_F(FrozenTest, RuntimeAndConstantEvaluationHashesAgree) {
  static constexpr auto kHashes = [] {
    std::array<std::uint64_t, kKeys.size()> hashes{};
    for (std::size_t index = 0; index < kKeys.size(); ++index) {
      hashes.at(index) = FrozenHash<std::string_view>{}(kKeys.at(index));
    }
    return hashes;
  }();
  for (std::size_t index = 0; index < kKeys.size(); ++index) {
    const std::string runtime(kKeys.at(index));
    EXPECT_THAT(FrozenHash<std::string_view>{}(runtime), kHashes.at(index));
  }
}

struct ForeignKey {
  int value;
};

struct TransparentHash {
  using is_transparent = void;  // NOLINT(readability-identifier-naming)

  constexpr std::uint64_t operator()(int value) const { return FrozenHash<int>{}(value); }

  constexpr std::uint64_t operator()(ForeignKey value) const { return (*this)(value.value); }
};

struct TransparentEqual {
  using is_transparent = void;  // NOLINT(readability-identifier-naming)

  constexpr bool operator()(int lhs, int rhs) const { return lhs == rhs; }

  constexpr bool operator()(int lhs, ForeignKey rhs) const { return lhs == rhs.value; }

  constexpr bool operator()(ForeignKey lhs, int rhs) const { return lhs.value == rhs; }
};

template<typename Container, typename Key>
concept HasFind = requires(const Container& container, const Key& key) { container.find(key); };
static_assert(!HasFind<FrozenSet<int, 2>, ForeignKey>);
static_assert(!HasFind<FrozenSet<int, 2, TransparentHash>, ForeignKey>);
static_assert(HasFind<FrozenSet<int, 2, TransparentHash, TransparentEqual>, ForeignKey>);

TEST_F(FrozenTest, TransparentOperationsDoNotConstructKeys) {
  static constexpr FrozenMap<int, int, 2, TransparentHash, TransparentEqual> kForeign({{1, 7}, {2, 8}});
  static constexpr FrozenSet<int, 2, TransparentHash, TransparentEqual> kForeignSet({1, 2});
  const ForeignKey query{1};
  EXPECT_THAT(kForeign.at(query), 7);
  EXPECT_THAT(kForeign.lookup(query), Optional(Eq(7)));
  EXPECT_THAT(kForeign.find(query)->second, 7);
  EXPECT_THAT(kForeign.contains(query), IsTrue());
  EXPECT_THAT(kForeign.count(query), 1);
  EXPECT_THAT(kForeign.index_of(query), 0);
  EXPECT_THAT(kForeign.bucket(query), kForeign.bucket(1));
  const auto [first, last] = kForeign.equal_range(query);
  EXPECT_THAT(std::ranges::subrange(first, last), ElementsAre(Pair(1, 7)));
  EXPECT_THAT(kForeignSet.lookup(query), Optional(Eq(1)));
  EXPECT_THAT(kForeignSet.find(ForeignKey{3}), kForeignSet.end());
}

TEST_F(FrozenTest, RuntimeConstructionOwnsElementsAndMatchesConstexprLayout) {
  auto entries = kPairs;
  const FrozenMap runtime(entries);
  entries.at(0).second = 99;
  EXPECT_THAT(runtime.at("-name"), 1);
  EXPECT_THAT(runtime, kMap);
  for (const auto& [key, value] : kPairs) {
    EXPECT_THAT(runtime.bucket(key), kMap.bucket(key));
    EXPECT_THAT(runtime.at(key), value);
  }
  EXPECT_THAT(runtime.construction_work(), kMap.construction_work());
}

TEST_F(FrozenTest, MapEqualityChecksKeysValuesAndSize) {
  static constexpr FrozenMap<int, int, 3> kFirst({{1, 2}, {3, 4}});
  static constexpr FrozenMap<int, int, 2> kReversed({{3, 4}, {1, 2}});
  static constexpr FrozenMap<int, int, 2> kValue({{1, 2}, {3, 5}});
  static constexpr FrozenMap<int, int, 2> kKey({{1, 2}, {4, 4}});
  static constexpr FrozenMap<int, int, 2> kShort({{1, 2}});
  EXPECT_THAT(kFirst == kReversed, IsTrue());
  EXPECT_THAT(kFirst == kValue, IsFalse());
  EXPECT_THAT(kFirst == kKey, IsFalse());
  EXPECT_THAT(kFirst == kShort, IsFalse());
}

TEST_F(FrozenTest, DefaultIteratorsAndCopiesAreIndependent) {
  const decltype(kSet)::iterator first;
  const decltype(kSet)::iterator second;
  EXPECT_THAT(first, second);
  auto copy = kSet;
  EXPECT_THAT(copy, kSet);
  EXPECT_THAT(&*copy.begin() == &*kSet.begin(), IsFalse());
  auto iterator = copy.begin();
  EXPECT_THAT(*iterator++, kKeys.front());
  EXPECT_THAT(*iterator, kKeys.at(1));
  const auto moved = std::move(copy);
  EXPECT_THAT(moved, kSet);
}

TEST_F(FrozenTest, MoveOnlyRuntimeElementsUseMoveIterators) {
  auto entries = std::to_array<std::pair<int, std::unique_ptr<int>>>({{1, std::make_unique<int>(7)}});
  const FrozenMap<int, std::unique_ptr<int>, 1> table(
      std::make_move_iterator(entries.begin()), std::make_move_iterator(entries.end()));
  EXPECT_THAT(*table.at(1), 7);
  EXPECT_THAT(entries.front().second, Eq(nullptr));
}

TEST_F(FrozenTest, ConstKeyArrayFactoriesAndSignedHashInputs) {
  static constexpr auto kConstKeys = std::to_array<std::pair<const std::string_view, int>>({{"key", 1}});
  static constexpr auto kConstMap = MakeFrozenMap(kConstKeys);
  static constexpr auto kConverted = ToFrozenMap(kConstKeys);
  static constexpr FrozenMap kDeduced(kConstKeys);
  EXPECT_THAT(kConstMap, kConverted);
  EXPECT_THAT(kConstMap, kDeduced);
  static constexpr FrozenSet<int, 3> kSigned({-1, 0, 1});
  EXPECT_THAT(kSigned.contains(-1), IsTrue());
  EXPECT_THAT(kSigned.contains(-2), IsFalse());
}

struct ConstHash {
  constexpr std::uint64_t operator()(int /*key*/) { return 0; }

  constexpr std::uint64_t operator()(int key) const { return FrozenHash<int>{}(key); }
};

struct ConstEqual {
  constexpr bool operator()(int /*lhs*/, int /*rhs*/) { return false; }

  constexpr bool operator()(int lhs, int rhs) const { return lhs == rhs; }
};

TEST_F(FrozenTest, ConstructionAndLookupUseTheSameConstHashAndEquality) {
  static constexpr FrozenSet<int, 2, ConstHash, ConstEqual> kConst({1, 2, 1});
  EXPECT_THAT(kConst.contains(1), IsTrue());
  EXPECT_THAT(kConst.contains(2), IsTrue());
  EXPECT_THAT(kConst.contains(3), IsFalse());
  EXPECT_THAT(kConst, SizeIs(2));
}

}  // namespace
}  // namespace mbo::container::experimental
