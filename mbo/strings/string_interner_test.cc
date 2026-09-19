// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/string_interner.h"

#include <array>
#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/hash/hash.h"

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

TEST_F(StringInternerTest, LocalIndexDiagnosticsDoNotIncludeParentOrPostCutoffEntries) {
  StringInterner<> root;
  EXPECT_THAT(root.intern("shared").index(), Eq(0));
  StringInterner<> child(&root);
  EXPECT_THAT(root.intern("late").index(), Eq(0));
  EXPECT_THAT(child.intern("local").index(), Eq(0));
  EXPECT_THAT(root.local_index_diagnostics().entries, Eq(2));
  EXPECT_THAT(child.local_index_diagnostics().entries, Eq(1));
  EXPECT_THAT(child.size(), Eq(2));
  EXPECT_THAT(child.find("late").has_value(), Eq(false));
  EXPECT_THAT(child.find("shared"), Optional(StringId<>(0)));
  EXPECT_THAT(child.find("local"), Optional(StringId<>(1)));
}

TEST_F(StringInternerTest, TracedSearchesReportDirectionDependentQueriesAndRespectCutoffs) {
  StringInterner<> root;
  EXPECT_THAT(root.intern("root").index(), Eq(0));
  StringInterner<> child(&root);
  EXPECT_THAT(child.intern("child").index(), Eq(0));
  StringInterner<> leaf(&child);
  EXPECT_THAT(leaf.intern("leaf").index(), Eq(0));
  EXPECT_THAT(root.intern("late").index(), Eq(0));
  const auto forward = leaf.trace_find("root");
  EXPECT_THAT(forward.id, Optional(StringId<>(0)));
  EXPECT_THAT(forward.parent_depth, Optional(std::size_t{2}));
  EXPECT_THAT(forward.index_queries, Eq(1));
  const auto reverse = leaf.trace_rfind("leaf");
  EXPECT_THAT(reverse.id, Optional(StringId<>(2)));
  EXPECT_THAT(reverse.parent_depth, Optional(std::size_t{0}));
  EXPECT_THAT(reverse.index_queries, Eq(1));
  EXPECT_THAT(leaf.trace_find("leaf").index_queries, Eq(3));
  EXPECT_THAT(leaf.trace_rfind("root").index_queries, Eq(3));
  EXPECT_THAT(leaf.trace_find("child").parent_depth, Optional(std::size_t{1}));
  EXPECT_THAT(leaf.trace_rfind("child").parent_depth, Optional(std::size_t{1}));
  const auto missing = leaf.trace_find("late");
  EXPECT_THAT(missing.id.has_value(), Eq(false));
  EXPECT_THAT(missing.parent_depth.has_value(), Eq(false));
  EXPECT_THAT(missing.index_queries, Eq(3));
  EXPECT_THAT(leaf.trace_rfind("late").id.has_value(), Eq(false));
}

TEST_F(StringInternerTest, MboHashKeepsHashWidthIndependentOfDenseIdWidth) {
  using Id = StringId<std::uint8_t>;
  using Index = HamtStringIndex<Id, mbo::hash::DefaultHasher>;
  using Interner =
      StringInterner<std::uint8_t, ArenaStringStorage<>, mbo::container::SegmentedSequence<std::string_view>, Index>;
  static_assert(sizeof(Id) == 1);
  static_assert(std::same_as<std::invoke_result_t<mbo::hash::DefaultHasher, std::string_view>, std::uint64_t>);
  Interner root;
  EXPECT_THAT(root.intern("").index(), Eq(0));
  EXPECT_THAT(root.intern(std::string_view("a\0b", 3)).index(), Eq(0));
  Interner child(&root);
  EXPECT_THAT(child.intern("a").index(), Eq(0));
  EXPECT_THAT(child.find(""), Optional(Id(0)));
  EXPECT_THAT(child.rfind(std::string_view("a\0b", 3)), Optional(Id(1)));
  EXPECT_THAT(child.find("a"), Optional(Id(2)));
  EXPECT_THAT(child.get(Id(1)), Optional(std::string_view("a\0b", 3)));
}

TEST_F(StringInternerTest, DeepChainsFilterEveryAncestorsLaterInsertionsInBothDirections) {
  constexpr std::size_t kDepth = 128;
  std::array<std::unique_ptr<StringInterner<>>, kDepth> chain;
  for (std::size_t depth = 0; depth < kDepth; ++depth) {
    chain.at(depth) = std::make_unique<StringInterner<>>(depth == 0 ? nullptr : chain.at(depth - 1).get());
    EXPECT_THAT(chain.at(depth)->intern("node" + std::to_string(depth)).index(), Eq(0));
  }
  for (std::size_t depth = 0; depth + 1 < kDepth; ++depth) {
    EXPECT_THAT(chain.at(depth)->intern("later").index(), Eq(0));
  }
  const auto& leaf = *chain.back();
  EXPECT_THAT(leaf.size(), Eq(kDepth));
  EXPECT_THAT(leaf.find("later").has_value(), Eq(false));
  EXPECT_THAT(leaf.rfind("later").has_value(), Eq(false));
  for (std::size_t depth = 0; depth < kDepth; ++depth) {
    const auto name = "node" + std::to_string(depth);
    const StringId<> id(static_cast<std::uint32_t>(depth));
    EXPECT_THAT(leaf.find(name), Optional(id));
    EXPECT_THAT(leaf.rfind(name), Optional(id));
    EXPECT_THAT(leaf.get(id), Optional(std::string_view(name)));
  }
  EXPECT_THAT(chain.back()->intern("later").index(), Eq(0));
  EXPECT_THAT(leaf.find("later"), Optional(StringId<>(kDepth)));
  EXPECT_THAT(leaf.rfind("later"), Optional(StringId<>(kDepth)));
  auto position = leaf.rbegin();
  EXPECT_THAT(*position++, Eq("later"));
  for (std::size_t depth = kDepth; depth > 0; --depth) {
    EXPECT_THAT(*position++, Eq("node" + std::to_string(depth - 1)));
  }
  EXPECT_THAT(position == leaf.rend(), Eq(true));
  // Honor the lifetime contract explicitly: destroy descendants first.
  for (auto owner = chain.rbegin(); owner != chain.rend(); ++owner) {
    owner->reset();
  }
}

TEST_F(StringInternerTest, RootIdentityAndParentGrowthDoNotChangeCapturedVisibility) {
  StringInterner<> root;
  StringInterner<> child(&root);
  const StringInterner<> grandchild(&child);
  EXPECT_THAT(root.root(), Eq(&root));
  EXPECT_THAT(child.root(), Eq(&root));
  EXPECT_THAT(grandchild.root(), Eq(&root));
  EXPECT_THAT(child.parent(), Eq(&root));
  EXPECT_THAT(child.first_local_id(), Eq(0));
  EXPECT_THAT(root.parent_has_grown(), Eq(false));
  EXPECT_THAT(child.parent_has_grown(), Eq(false));
  EXPECT_THAT(root.intern("root-later").index(), Eq(0));
  EXPECT_THAT(child.parent_has_grown(), Eq(true));
  EXPECT_THAT(grandchild.parent_has_grown(), Eq(false));
  EXPECT_THAT(child.find("root-later").has_value(), Eq(false));
  EXPECT_THAT(child.intern("child-later").index(), Eq(0));
  EXPECT_THAT(grandchild.parent_has_grown(), Eq(true));
  EXPECT_THAT(grandchild.find("child-later").has_value(), Eq(false));
  EXPECT_THAT(child.intern("child-later").index(), Eq(0));
  EXPECT_THAT(grandchild.parent_has_grown(), Eq(true));
}

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

struct FailAfterCopyStorage final {
  // These names intentionally implement StringInternerStorage's STL-style protocol.
  using checkpoint_type = ArenaStringStorage<>::checkpoint_type;  // NOLINT(readability-identifier-naming)

  std::optional<std::string_view> try_store(  // NOLINT(readability-identifier-naming)
      std::string_view text) noexcept {
    const auto stored = storage.try_store(text);
    if (fail_next) {
      fail_next = false;
      return std::nullopt;
    }
    return stored;
  }

  checkpoint_type checkpoint() const noexcept {  // NOLINT(readability-identifier-naming)
    return storage.checkpoint();
  }

  void rewind(const checkpoint_type& checkpoint) noexcept {  // NOLINT(readability-identifier-naming)
    storage.rewind(checkpoint);
  }

  std::size_t bytes_used() const noexcept {  // NOLINT(readability-identifier-naming)
    return storage.bytes_used();
  }

  ArenaStringStorage<> storage;
  bool fail_next = true;
};

TEST_F(StringInternerTest, CharacterFailureRewindsUncommittedBytesBeforeRetry) {
  StringInterner<std::uint32_t, FailAfterCopyStorage> interner;
  EXPECT_THAT(
      interner.intern("uncommitted"), VariantWith<StringInternError>(StringInternError::kCharacterStorageExhausted));
  EXPECT_THAT(interner.local_character_bytes_used(), Optional(0));
  EXPECT_THAT(interner.size(), Eq(0));
  EXPECT_THAT(interner.find("uncommitted").has_value(), Eq(false));
  EXPECT_THAT(interner.intern("retry").index(), Eq(0));
  EXPECT_THAT(interner.local_character_bytes_used(), Optional(5));
  EXPECT_THAT(interner.get(StringId<>(0)), Optional(std::string_view("retry")));
}

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
  using EmptyStorage = ArenaStringStorage<mbo::memory::Arena<mbo::memory::InlineBlockSource<1>>>;
  StringInterner<std::uint32_t, EmptyStorage> bounded;
  EXPECT_THAT(bounded.intern("x"), VariantWith<StringInternError>(StringInternError::kCharacterStorageExhausted));
  EXPECT_THAT(bounded.size(), Eq(0));
  EXPECT_THAT(bounded.intern("").index(), Eq(0));
  EXPECT_THAT(bounded.get(StringId<>(0)), Optional(std::string_view{}));
  using Entries = mbo::container::SegmentedSequence<
      std::string_view, mbo::container::SegmentedSequenceOptions{.segment_capacities = {1}, .maximum_size = 1}>;
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

template<typename Interner>
concept SupportsLocalIndexDiagnostics = requires(const Interner& interner) {
  { interner.local_index_diagnostics() } noexcept;
};

static_assert(SupportsLocalIndexDiagnostics<StringInterner<>>);
static_assert(!SupportsLocalIndexDiagnostics<StringInterner<
                  std::uint32_t,
                  ArenaStringStorage<>,
                  mbo::container::SegmentedSequence<std::string_view>,
                  FailFirstIndex>>);

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
