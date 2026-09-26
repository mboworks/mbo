// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/string_interner.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::strings {
namespace {
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Optional;
using ::testing::VariantWith;

struct StringInternerTest : ::testing::Test {};

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
    for (std::size_t position = 0; position < size; ++position) {
      if (entries[position].first == key) {
        return entries[position].second;
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
    entries[size++] = {key, identifier};
    return true;
  }

  std::array<std::pair<std::string_view, Id>, Capacity> entries{};
  std::size_t size = 0;
};

template<
    StringIdRepresentation Representation = std::uint32_t,
    std::size_t CharacterCapacity = 16'384,
    std::size_t StringCapacity = 512,
    typename Entries = mbo::container::SegmentedSequence<std::string_view>>
using TestStringInterner = StringInterner<
    FixedStringContainer<StringId<Representation>, StringCapacity>,
    FixedStringStorage<CharacterCapacity>,
    Representation,
    Entries>;

static_assert(std::bidirectional_iterator<TestStringInterner<>::iterator>);

TEST_F(StringInternerTest, CharacterAndEntryExhaustionLeavePublishedStringsUnchanged) {
  TestStringInterner<std::uint32_t, 0> bounded;
  EXPECT_THAT(bounded.intern("x"), VariantWith<StringInternError>(StringInternError::kCharacterStorageExhausted));
  EXPECT_THAT(bounded.size(), Eq(0));
  EXPECT_THAT(bounded.intern("").index(), Eq(0));
  EXPECT_THAT(bounded.get(StringId<>(0)), Optional(std::string_view{}));

  using Entries = mbo::container::SegmentedSequence<
      std::string_view,
      mbo::container::SegmentedSequenceOptions{.segment_size = 1, .segment_capacity = 1, .segment_reservation = 1}>;
  TestStringInterner<std::uint32_t, 64, 8, Entries> one;
  EXPECT_THAT(one.intern("first").index(), Eq(0));
  const auto original = one.get(StringId<>(0)).value_or(std::string_view{});
  EXPECT_THAT(one.intern("second"), VariantWith<StringInternError>(StringInternError::kEntryStorageExhausted));
  EXPECT_THAT(one.size(), Eq(1));
  EXPECT_THAT(original, Eq("first"));
  EXPECT_THAT(one.find("second"), Eq(std::nullopt));
}

TEST_F(StringInternerTest, EmbeddedNulsAndExistingIteratorsSurviveInputMutationAndAppend) {
  TestStringInterner<> interner;
  std::string text("a\0b", 3);
  EXPECT_THAT(interner.intern_child_first(text).index(), Eq(0));
  auto position = interner.begin();
  const auto original = *position;
  text.assign("changed");
  EXPECT_THAT(interner.intern_parent_first("a").index(), Eq(0));
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
  EXPECT_THAT(interner.intern("failed"), VariantWith<StringInternError>(StringInternError::kIndexExhausted));
  EXPECT_THAT(interner.size(), Eq(0));
  EXPECT_THAT(interner.find("failed"), Eq(std::nullopt));
  using Inserted = std::pair<StringId<>, bool>;
  EXPECT_THAT(interner.intern("retry"), VariantWith<Inserted>(std::pair(StringId<>(0), true)));
  EXPECT_THAT(interner.get(StringId<>(0)), Optional(std::string_view("retry")));
}

TEST_F(StringInternerTest, RejectedStringContainerInsertionRollsBackAndCanRetryAtZero) {
  using Interner = StringInterner<RejectFirstStringContainer, FixedStringStorage<64>>;
  Interner interner;
  EXPECT_THAT(interner.intern("rejected"), VariantWith<StringInternError>(StringInternError::kIndexExhausted));
  EXPECT_THAT(interner.size(), Eq(0));
  EXPECT_THAT(interner.find("rejected"), Eq(std::nullopt));
  using Inserted = std::pair<StringId<>, bool>;
  EXPECT_THAT(interner.intern("retry"), VariantWith<Inserted>(std::pair(StringId<>(0), true)));
  EXPECT_THAT(interner.get(StringId<>(0)), Optional(std::string_view("retry")));
}

TEST_F(StringInternerTest, EightBitIdsReserveInvalidValueAndFindDuplicatesAfterExhaustion) {
  TestStringInterner<std::uint8_t> interner;
  for (unsigned value = 0; value < StringId<std::uint8_t>::invalid_value; ++value) {
    EXPECT_THAT(interner.intern(std::to_string(value)).index(), Eq(0));
  }
  EXPECT_THAT(interner.size(), Eq(255));
  EXPECT_THAT(interner.get(StringId<std::uint8_t>(254)), Optional(std::string_view("254")));
  EXPECT_THAT(interner.get(StringId<std::uint8_t>{}), Eq(std::nullopt));
  EXPECT_THAT(interner.intern("exhausted"), VariantWith<StringInternError>(StringInternError::kIdExhausted));
  EXPECT_THAT(interner.intern("0").index(), Eq(0));
  EXPECT_THAT(interner.size(), Eq(255));
}

TEST_F(StringInternerTest, EmptyDeclaredParentRetainsIdentityWithoutExposingLaterValues) {
  TestStringInterner<> root;
  TestStringInterner<> child(&root);
  EXPECT_THAT(child.parent(), Eq(&root));
  EXPECT_THAT(child.first_local_id(), Eq(0));
  EXPECT_THAT(root.intern("later").index(), Eq(0));
  EXPECT_THAT(child.find("later"), Eq(std::nullopt));
  EXPECT_THAT(child.rfind("later"), Eq(std::nullopt));
  EXPECT_THAT(child.intern("local").index(), Eq(0));
  EXPECT_THAT(child.get(StringId<>(0)), Optional(std::string_view("local")));
}

TEST_F(StringInternerTest, IterationIncludesCapturedAncestorsAndPreservesBranchIdentity) {
  TestStringInterner<> root;
  EXPECT_THAT(root.intern("root").index(), Eq(0));
  TestStringInterner<> child(&root);
  EXPECT_THAT(child.intern("child").index(), Eq(0));
  TestStringInterner<> grandchild(&child);
  EXPECT_THAT(child.intern("invisible").index(), Eq(0));
  EXPECT_THAT(grandchild.intern("grandchild").index(), Eq(0));
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
  EXPECT_THAT(root.intern("root").index(), Eq(0));
  TestStringInterner<> child(&root);
  EXPECT_THAT(child.intern("child").index(), Eq(0));
  EXPECT_THAT(root.intern("later").index(), Eq(0));
  EXPECT_THAT(child.find("later"), Eq(std::nullopt));
  EXPECT_THAT(child.intern("later").index(), Eq(0));
  EXPECT_THAT(root.find("later"), Optional(StringId<>(1)));
  EXPECT_THAT(child.find("later"), Optional(StringId<>(2)));
  EXPECT_THAT(child.find("root"), Optional(StringId<>(0)));
  EXPECT_THAT(child.rfind("root"), Optional(StringId<>(0)));
  EXPECT_THAT(child.get(StringId<>(1)), Optional(std::string_view("child")));
  EXPECT_THAT(child.size(), Eq(3));
  EXPECT_THAT(child.local_size(), Eq(2));
}

}  // namespace
}  // namespace mbo::strings
