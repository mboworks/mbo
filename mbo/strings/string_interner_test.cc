// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/string_interner.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::strings {
namespace {
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Optional;
using ::testing::VariantWith;

struct StringInternerTest : ::testing::Test {};

static_assert(noexcept(*std::declval<StringInterner<>::iterator&>()) == !::mbo::config::kRequireThrows);
static_assert(noexcept(++std::declval<StringInterner<>::iterator&>()) == !::mbo::config::kRequireThrows);
static_assert(noexcept(--std::declval<StringInterner<>::iterator&>()) == !::mbo::config::kRequireThrows);

static_assert(std::bidirectional_iterator<StringInterner<>::iterator>);

TEST_F(StringInternerTest, IteratorBoundariesSupportEmptyStringsAndDecrementingEnd) {
  StringInterner<> interner;
  EXPECT_THAT(interner.begin() == interner.end(), Eq(true));
  EXPECT_THAT(interner.rbegin() == interner.rend(), Eq(true));
  EXPECT_THAT(interner.intern("").index(), Eq(0));
  auto position = interner.end();
  EXPECT_THAT(*--position, Eq(std::string_view{}));
  EXPECT_THAT(position == interner.begin(), Eq(true));
  EXPECT_THAT(++position == interner.end(), Eq(true));
}

#ifndef NDEBUG
TEST_F(StringInternerTest, InvalidIteratorOperationsFailDebugContracts) {
  const StringInterner<> interner;
  const StringInterner<>::iterator singular;
  EXPECT_DEATH(static_cast<void>(*singular), "singular StringInterner iterator");
  EXPECT_DEATH(static_cast<void>(*interner.end()), "StringInterner end iterator");
  EXPECT_DEATH(static_cast<void>(++interner.end()), "StringInterner end iterator");
  EXPECT_DEATH(static_cast<void>(--interner.begin()), "StringInterner begin iterator");
}
#endif

// Implements ownership and rollback without promising memory accounting.
struct UnmeasuredStorage final {
  // These names intentionally implement StringInternerStorage's STL-style protocol.
  using checkpoint_type = ArenaStringStorage<>::checkpoint_type;  // NOLINT(readability-identifier-naming)

  std::optional<std::string_view> try_store(  // NOLINT(readability-identifier-naming)
      std::string_view text) noexcept {
    return storage.try_store(text);
  }

  checkpoint_type checkpoint() const noexcept {  // NOLINT(readability-identifier-naming)
    return storage.checkpoint();
  }

  void rewind(const checkpoint_type& checkpoint) noexcept {  // NOLINT(readability-identifier-naming)
    storage.rewind(checkpoint);
  }

  ArenaStringStorage<> storage;
};

TEST_F(StringInternerTest, UnsupportedStorageStatisticsAreUnknownRatherThanZero) {
  StringInterner<std::uint32_t, UnmeasuredStorage> interner;
  EXPECT_THAT(interner.local_character_bytes_used().has_value(), Eq(false));
  EXPECT_THAT(interner.local_character_bytes_reserved().has_value(), Eq(false));
  EXPECT_THAT(interner.intern("owned").index(), Eq(0));
  EXPECT_THAT(interner.local_character_bytes_used().has_value(), Eq(false));
  std::size_t measured = 0;
  interner.visit_string_sizes([&](std::size_t size) noexcept { measured += size; });
  EXPECT_THAT(measured, Eq(5));
}

TEST_F(StringInternerTest, SizeDiagnosticsRespectCapturedPrefixesAndLocalMemoryOwnership) {
  StringInterner<> root;
  EXPECT_THAT(root.intern("").index(), Eq(0));
  EXPECT_THAT(root.intern("abc").index(), Eq(0));
  StringInterner<> child(&root);
  EXPECT_THAT(root.intern("invisible").index(), Eq(0));
  EXPECT_THAT(child.intern(std::string_view("a\0", 2)).index(), Eq(0));
  std::size_t count = 0;
  std::size_t total = 0;
  std::size_t empty = 0;
  child.visit_string_sizes([&](std::size_t size) noexcept {
    ++count;
    total += size;
    empty += size == 0 ? 1 : 0;
  });
  EXPECT_THAT(count, Eq(3));
  EXPECT_THAT(total, Eq(5));
  EXPECT_THAT(empty, Eq(1));
  EXPECT_THAT(child.local_character_bytes_used(), Optional(2));
  EXPECT_THAT(child.local_character_bytes_reserved().value_or(0) >= 2, Eq(true));
  const StringInterner<> unused;
  unused.visit_string_sizes([&](std::size_t) noexcept { ++count; });
  EXPECT_THAT(count, Eq(3));
  EXPECT_THAT(unused.local_character_bytes_used(), Optional(0));
}

TEST_F(StringInternerTest, FactoriesConstructAllBackendsOnceWithoutMovingCharacterStorage) {
  int storage_calls = 0;
  int entry_calls = 0;
  int index_calls = 0;
  StringInterner<> interner(
      nullptr,
      [&storage_calls]() noexcept {
        ++storage_calls;
        return ArenaStringStorage<>();
      },
      [&entry_calls]() noexcept {
        ++entry_calls;
        return mbo::container::SegmentedSequence<std::string_view>();
      },
      [&index_calls]() noexcept {
        ++index_calls;
        return HamtStringIndex<>();
      });
  EXPECT_THAT(storage_calls, Eq(1));
  EXPECT_THAT(entry_calls, Eq(1));
  EXPECT_THAT(index_calls, Eq(1));
  EXPECT_THAT(interner.intern("configured").index(), Eq(0));
  EXPECT_THAT(interner.get(StringId<>(0)), Optional(std::string_view("configured")));
}

TEST_F(StringInternerTest, CharacterAndEntryExhaustionLeavePublishedStringsUnchanged) {
  constexpr mbo::memory::ArenaOptions kEmptyArenaOptions{
      .initial_block_size = 16,
      .maximum_block_size = 16,
      .growth_numerator = 1,
      .growth_denominator = 1,
  };
  using EmptyStorage = ArenaStringStorage<mbo::memory::Arena<mbo::memory::InlineBlockSource<1>, kEmptyArenaOptions>>;
  StringInterner<std::uint32_t, EmptyStorage> bounded;
  EXPECT_THAT(bounded.intern("x"), VariantWith<StringInternError>(StringInternError::kCharacterStorageExhausted));
  EXPECT_THAT(bounded.size(), Eq(0));
  EXPECT_THAT(bounded.intern("").index(), Eq(0));
  EXPECT_THAT(bounded.get(StringId<>(0)), Optional(std::string_view{}));
  using Entries = mbo::container::SegmentedSequence<
      std::string_view,
      mbo::container::SegmentedSequenceOptions{.segment_size = 1, .segment_capacity = 1, .segment_reservation = 1}>;
  StringInterner<std::uint32_t, ArenaStringStorage<>, Entries> one;
  EXPECT_THAT(one.intern("first").index(), Eq(0));
  const auto original = one.get(StringId<>(0)).value_or(std::string_view{});
  EXPECT_THAT(one.intern("second"), VariantWith<StringInternError>(StringInternError::kEntryStorageExhausted));
  EXPECT_THAT(one.size(), Eq(1));
  EXPECT_THAT(original, Eq("first"));
  EXPECT_THAT(one.find("second").has_value(), Eq(false));
}

TEST_F(StringInternerTest, EmbeddedNulsAndExistingIteratorsSurviveInputMutationAndAppend) {
  StringInterner<> interner;
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

// NOLINTBEGIN(readability-identifier-naming): test double models the string-index adapter.
struct FailFirstIndex final {
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

struct RejectFirstIndex final {
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

TEST_F(StringInternerTest, FailedIndexInsertionRollsBackTheDenseEntryAndCanRetryAtZero) {
  using Interner = StringInterner<
      std::uint32_t, ArenaStringStorage<>, mbo::container::SegmentedSequence<std::string_view>, FailFirstIndex>;
  Interner interner;
  EXPECT_THAT(interner.intern("failed"), VariantWith<StringInternError>(StringInternError::kIndexExhausted));
  EXPECT_THAT(interner.size(), Eq(0));
  EXPECT_THAT(interner.find("failed").has_value(), Eq(false));
  using Inserted = std::pair<StringId<>, bool>;
  EXPECT_THAT(interner.intern("retry"), VariantWith<Inserted>(std::pair(StringId<>(0), true)));
  EXPECT_THAT(interner.get(StringId<>(0)), Optional(std::string_view("retry")));
}

TEST_F(StringInternerTest, RejectedIndexInsertionRollsBackTheDenseEntryAndCanRetryAtZero) {
  using Interner = StringInterner<
      std::uint32_t, ArenaStringStorage<>, mbo::container::SegmentedSequence<std::string_view>, RejectFirstIndex>;
  Interner interner;
  EXPECT_THAT(interner.intern("rejected"), VariantWith<StringInternError>(StringInternError::kIndexExhausted));
  EXPECT_THAT(interner.size(), Eq(0));
  EXPECT_THAT(interner.find("rejected").has_value(), Eq(false));
  using Inserted = std::pair<StringId<>, bool>;
  EXPECT_THAT(interner.intern("retry"), VariantWith<Inserted>(std::pair(StringId<>(0), true)));
  EXPECT_THAT(interner.get(StringId<>(0)), Optional(std::string_view("retry")));
}

TEST_F(StringInternerTest, EightBitIdsReserveInvalidValueAndFindDuplicatesAfterExhaustion) {
  StringInterner<std::uint8_t> interner;
  for (unsigned value = 0; value < StringId<std::uint8_t>::invalid_value; ++value) {
    const auto text = std::to_string(value);
    EXPECT_THAT(interner.intern(text).index(), Eq(0));
  }
  EXPECT_THAT(interner.size(), Eq(255));
  EXPECT_THAT(interner.get(StringId<std::uint8_t>(254)), Optional(std::string_view("254")));
  EXPECT_THAT(interner.get(StringId<std::uint8_t>{}), Eq(std::nullopt));
  EXPECT_THAT(interner.intern("exhausted"), VariantWith<StringInternError>(StringInternError::kIdExhausted));
  EXPECT_THAT(interner.intern("0").index(), Eq(0));
  EXPECT_THAT(interner.size(), Eq(255));
}

TEST_F(StringInternerTest, EmptyDeclaredParentRetainsIdentityWithoutExposingLaterValues) {
  StringInterner<> root;
  StringInterner<> child(&root);
  EXPECT_THAT(child.parent(), Eq(&root));
  EXPECT_THAT(child.first_local_id(), Eq(0));
  EXPECT_THAT(root.intern("later").index(), Eq(0));
  EXPECT_THAT(child.find("later").has_value(), Eq(false));
  EXPECT_THAT(child.rfind("later").has_value(), Eq(false));
  EXPECT_THAT(child.intern("local").index(), Eq(0));
  EXPECT_THAT(child.get(StringId<>(0)), Optional(std::string_view("local")));
}

TEST_F(StringInternerTest, IterationIncludesCapturedAncestorsAndPreservesBranchIdentity) {
  StringInterner<> root;
  EXPECT_THAT(root.intern("root").index(), Eq(0));
  StringInterner<> child(&root);
  EXPECT_THAT(child.intern("child").index(), Eq(0));
  StringInterner<> grandchild(&child);
  EXPECT_THAT(child.intern("invisible").index(), Eq(0));
  EXPECT_THAT(grandchild.intern("grandchild").index(), Eq(0));
  EXPECT_THAT(grandchild, ElementsAre("root", "child", "grandchild"));
  auto reverse = grandchild.rbegin();
  EXPECT_THAT(*reverse++, Eq("grandchild"));
  EXPECT_THAT(*reverse++, Eq("child"));
  EXPECT_THAT(*reverse++, Eq("root"));
  EXPECT_THAT(reverse == grandchild.rend(), Eq(true));
  EXPECT_THAT(root.begin() == child.begin(), Eq(false));
  EXPECT_THAT(grandchild.find("invisible").has_value(), Eq(false));
  EXPECT_THAT(grandchild.rfind("invisible").has_value(), Eq(false));
}

TEST_F(StringInternerTest, ChildCapturesPrefixAndIgnoresLaterParentInsertions) {
  StringInterner<> root;
  EXPECT_THAT(root.intern("root").index(), Eq(0));
  StringInterner<> child(&root);
  EXPECT_THAT(child.intern("child").index(), Eq(0));
  EXPECT_THAT(root.intern("later").index(), Eq(0));
  EXPECT_THAT(child.find("later").has_value(), Eq(false));
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
