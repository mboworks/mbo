// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/string_interner.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/strings/arena_string_storage.h"

namespace mbo::strings {
namespace {
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Optional;
using ::testing::VariantWith;

struct StringInternerTest : ::testing::Test {};

// NOLINTBEGIN(readability-identifier-naming): test doubles model the interner's STL-style contracts.
template<std::size_t Capacity>
struct FixedStringStorage final {
  using checkpoint_type = std::size_t;

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

  checkpoint_type checkpoint() const noexcept { return size; }

  void rewind(checkpoint_type checkpoint) noexcept { size = checkpoint; }

  std::array<char, Capacity> bytes{};
  std::size_t size = 0;
};

template<typename Id, std::size_t Capacity>
struct FixedStringContainer final {
  std::optional<Id> find(std::string_view key) const noexcept {
    for (const auto& entry : std::span(entries).first(size)) {
      if (entry.first == key) {
        return entry.second;
      }
    }
    return std::nullopt;
  }

  std::optional<bool> try_insert(std::string_view key, Id identifier) noexcept {
    if (find(key)) {
      return false;
    }
    if (size == Capacity) {
      return std::nullopt;
    }
    entries.at(size++) = {key, identifier};
    return true;
  }

  std::array<std::pair<std::string_view, Id>, Capacity> entries{};
  std::size_t size = 0;
};

// NOLINTEND(readability-identifier-naming)

template<
    StringInternerId Id = StringId<>,
    std::size_t CharacterCapacity = 16'384,
    std::size_t StringCapacity = 512,
    typename Entries = mbo::container::SegmentedSequence<std::string_view>>
using TestStringInterner =
    StringInterner<FixedStringContainer<Id, StringCapacity>, FixedStringStorage<CharacterCapacity>, Id, Entries>;

static_assert(std::bidirectional_iterator<TestStringInterner<>::iterator>);
static_assert(StringInternerId<std::int16_t>);
static_assert(StringInternerId<std::uint16_t>);
static_assert(StringInternerId<StringId<std::int16_t>>);
static_assert(StringInternerId<StringId<std::uint16_t>>);

TEST_F(StringInternerTest, CharacterAndEntryExhaustionLeavePublishedStringsUnchanged) {
  TestStringInterner<StringId<>, 0> bounded;
  EXPECT_THAT(bounded.insert("x"), VariantWith<StringInternError>(StringInternError::kCharacterStorageExhausted));
  EXPECT_THAT(bounded.size(), Eq(0));
  EXPECT_THAT(bounded.insert("").index(), Eq(0));
  EXPECT_THAT(bounded.get(StringId<>(0)), Optional(std::string_view{}));

  using Entries = mbo::container::SegmentedSequence<
      std::string_view,
      mbo::container::SegmentedSequenceOptions{.segment_size = 1, .segment_capacity = 1, .segment_reservation = 1}>;
  TestStringInterner<StringId<>, 64, 8, Entries> one;
  EXPECT_THAT(one.insert("first").index(), Eq(0));
  const auto original = one.get(StringId<>(0)).value_or(std::string_view{});
  EXPECT_THAT(one.insert("second"), VariantWith<StringInternError>(StringInternError::kEntryStorageExhausted));
  EXPECT_THAT(one.size(), Eq(1));
  EXPECT_THAT(original, Eq("first"));
  EXPECT_THAT(one.find("second"), Eq(std::nullopt));
}

TEST_F(StringInternerTest, EmbeddedNulsAndExistingIteratorsSurviveInputMutationAndAppend) {
  TestStringInterner<> interner;
  std::string text("a\0b", 3);
  EXPECT_THAT(interner.insert_child_first(text).index(), Eq(0));
  auto position = interner.begin();
  const auto original = *position;
  text.assign("changed");
  EXPECT_THAT(interner.insert_parent_first("a").index(), Eq(0));
  EXPECT_THAT(original, Eq(std::string_view("a\0b", 3)));
  EXPECT_THAT(*position, Eq(original));
  EXPECT_THAT(interner.find(original), Optional(StringId<>(0)));
  EXPECT_THAT(interner.rfind("a"), Optional(StringId<>(1)));
  ++position;
  EXPECT_THAT(*position, Eq("a"));
  ++position;
  EXPECT_THAT(position == interner.end(), Eq(true));
}

// NOLINTBEGIN(readability-identifier-naming): test doubles model the string-container contract.
struct FailFirstStringContainer final {
  std::optional<std::pair<std::string_view, StringId<>>> entry;
  bool failed = false;

  std::optional<StringId<>> find(std::string_view key) const noexcept {
    return entry && entry->first == key ? std::optional(entry->second) : std::nullopt;
  }

  std::optional<bool> try_insert(std::string_view key, StringId<> identifier) noexcept {
    if (!failed) {
      failed = true;
      return std::nullopt;
    }
    entry.emplace(key, identifier);
    return true;
  }
};

struct RejectFirstStringContainer final {
  std::optional<std::pair<std::string_view, StringId<>>> entry;
  bool rejected = false;

  std::optional<StringId<>> find(std::string_view key) const noexcept {
    return entry && entry->first == key ? std::optional(entry->second) : std::nullopt;
  }

  std::optional<bool> try_insert(std::string_view key, StringId<> identifier) noexcept {
    if (!rejected) {
      rejected = true;
      return false;
    }
    entry.emplace(key, identifier);
    return true;
  }
};

// NOLINTEND(readability-identifier-naming)

TEST_F(StringInternerTest, FailedStringContainerInsertionRollsBackAndCanRetryAtZero) {
  using Interner = StringInterner<FailFirstStringContainer, FixedStringStorage<64>>;
  Interner interner;
  EXPECT_THAT(interner.insert("failed"), VariantWith<StringInternError>(StringInternError::kIndexExhausted));
  EXPECT_THAT(interner.size(), Eq(0));
  EXPECT_THAT(interner.find("failed"), Eq(std::nullopt));
  using Inserted = std::pair<StringId<>, bool>;
  EXPECT_THAT(interner.insert("retry"), VariantWith<Inserted>(std::pair(StringId<>(0), true)));
  EXPECT_THAT(interner.get(StringId<>(0)), Optional(std::string_view("retry")));
}

TEST_F(StringInternerTest, RejectedStringContainerInsertionRollsBackAndCanRetryAtZero) {
  using Interner = StringInterner<RejectFirstStringContainer, FixedStringStorage<64>>;
  Interner interner;
  EXPECT_THAT(interner.insert("rejected"), VariantWith<StringInternError>(StringInternError::kIndexExhausted));
  EXPECT_THAT(interner.size(), Eq(0));
  EXPECT_THAT(interner.find("rejected"), Eq(std::nullopt));
  using Inserted = std::pair<StringId<>, bool>;
  EXPECT_THAT(interner.insert("retry"), VariantWith<Inserted>(std::pair(StringId<>(0), true)));
  EXPECT_THAT(interner.get(StringId<>(0)), Optional(std::string_view("retry")));
}

TEST_F(StringInternerTest, EightBitIdsReserveInvalidValueAndFindDuplicatesAfterExhaustion) {
  TestStringInterner<StringId<std::uint8_t>> interner;
  for (unsigned value = 0; value < StringId<std::uint8_t>::invalid_value; ++value) {
    EXPECT_THAT(interner.insert(std::to_string(value)).index(), Eq(0));
  }
  EXPECT_THAT(interner.size(), Eq(255));
  EXPECT_THAT(interner.get(StringId<std::uint8_t>(254)), Optional(std::string_view("254")));
  EXPECT_THAT(interner.get(StringId<std::uint8_t>{}), Eq(std::nullopt));
  EXPECT_THAT(interner.insert("exhausted"), VariantWith<StringInternError>(StringInternError::kIdExhausted));
  EXPECT_THAT(interner.insert("0").index(), Eq(0));
  EXPECT_THAT(interner.size(), Eq(255));
}

TEST_F(StringInternerTest, EmptyDeclaredParentRetainsIdentityWithoutExposingLaterValues) {
  TestStringInterner<> root;
  TestStringInterner<> child(&root);
  EXPECT_THAT(child.parent(), Eq(&root));
  EXPECT_THAT(child.first_local_id(), Eq(0));
  EXPECT_THAT(root.insert("later").index(), Eq(0));
  EXPECT_THAT(child.find("later"), Eq(std::nullopt));
  EXPECT_THAT(child.rfind("later"), Eq(std::nullopt));
  EXPECT_THAT(child.insert("local").index(), Eq(0));
  EXPECT_THAT(child.get(StringId<>(0)), Optional(std::string_view("local")));
}

TEST_F(StringInternerTest, IterationIncludesCapturedAncestorsAndPreservesBranchIdentity) {
  TestStringInterner<> root;
  EXPECT_THAT(root.insert("root").index(), Eq(0));
  TestStringInterner<> child(&root);
  EXPECT_THAT(child.insert("child").index(), Eq(0));
  TestStringInterner<> grandchild(&child);
  EXPECT_THAT(child.insert("invisible").index(), Eq(0));
  EXPECT_THAT(grandchild.insert("grandchild").index(), Eq(0));
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
  EXPECT_THAT(root.insert("root").index(), Eq(0));
  TestStringInterner<> child(&root);
  EXPECT_THAT(child.insert("child").index(), Eq(0));
  EXPECT_THAT(root.insert("later").index(), Eq(0));
  EXPECT_THAT(child.find("later"), Eq(std::nullopt));
  EXPECT_THAT(child.insert("later").index(), Eq(0));
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
  EXPECT_THAT(interner.insert("first").index(), Eq(0));
  EXPECT_THAT(interner.insert("second").index(), Eq(0));
  EXPECT_THAT(interner, ElementsAre("first", "second"));
  EXPECT_THAT(interner.get(StringId<>(1)), Optional(std::string_view("second")));
}

TEST_F(StringInternerTest, SupportsSegmentedListDequeAndVectorEntryContainers) {
  ExpectEntryContainerSupport<mbo::container::SegmentedSequence<std::string_view>>();
  ExpectEntryContainerSupport<std::list<std::string_view>>();
  ExpectEntryContainerSupport<std::deque<std::string_view>>();
  ExpectEntryContainerSupport<std::vector<std::string_view>>();
}

TEST_F(StringInternerTest, SupportsArenaCharacterStorage) {
  using Interner = StringInterner<
      FixedStringContainer<StringId<>, 8>, ArenaStringStorage<>, StringId<>, std::vector<std::string_view>>;
  Interner interner;
  std::string input = "arena-owned";
  EXPECT_THAT(interner.insert(input).index(), Eq(0));
  input.assign("changed");
  EXPECT_THAT(interner.get(StringId<>(0)), Optional(std::string_view("arena-owned")));
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
  EXPECT_THAT(plain_signed.insert("plain-signed"), VariantWith<PlainSignedResult>(PlainSignedResult{0, true}));
  EXPECT_THAT(plain_unsigned.insert("plain-unsigned"), VariantWith<PlainUnsignedResult>(PlainUnsignedResult{0, true}));
  EXPECT_THAT(
      string_signed.insert("string-signed"),
      VariantWith<StringSignedResult>(StringSignedResult{StringId<std::int16_t>{0}, true}));
  EXPECT_THAT(
      string_unsigned.insert("string-unsigned"),
      VariantWith<StringUnsignedResult>(StringUnsignedResult{StringId<std::uint16_t>{0}, true}));
  EXPECT_THAT(plain_signed.get(-1), Eq(std::nullopt));
  EXPECT_THAT(string_signed.get(StringId<std::int16_t>{-1}), Eq(std::nullopt));
}

}  // namespace
}  // namespace mbo::strings
