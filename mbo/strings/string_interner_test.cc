// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/string_interner.h"

#include <array>
#include <exception>
#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/limited_vector.h"
#include "mbo/hash/hash.h"
#include "mbo/memory/arena.h"
#include "mbo/memory/block_source.h"

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
static_assert(noexcept(std::declval<StringInterner<>&>().intern(std::string_view{})) == !::mbo::config::kRequireThrows);
static_assert(
    noexcept(std::declval<StringInterner<>&>().intern_parent_first(std::string_view{}))
    == !::mbo::config::kRequireThrows);
static_assert(
    noexcept(std::declval<StringInterner<>&>().intern_child_first(std::string_view{}))
    == !::mbo::config::kRequireThrows);
static_assert(
    noexcept(std::declval<StringInterner<>&>().try_intern(std::string_view{})) == !::mbo::config::kRequireThrows);
static_assert(
    noexcept(std::declval<StringInterner<>&>().try_intern_id(std::string_view{})) == !::mbo::config::kRequireThrows);

// These syntax-only backends differ solely in their destructor specification.
template<bool NothrowDestruction>
struct DestructionBackend final {
  using checkpoint_type = std::size_t;
  using value_type = std::string_view;

  DestructionBackend() = default;
  DestructionBackend(const DestructionBackend&) = default;
  DestructionBackend& operator=(const DestructionBackend&) = default;
  DestructionBackend(DestructionBackend&&) noexcept = default;
  DestructionBackend& operator=(DestructionBackend&&) noexcept = default;

  ~DestructionBackend() noexcept(NothrowDestruction) = default;

  static std::optional<StringId<>> find(std::string_view) noexcept { return std::nullopt; }

  static std::optional<bool> try_insert(std::string_view, StringId<>) noexcept { return true; }

  static std::optional<std::string_view> try_store(std::string_view text) noexcept { return text; }

  // NOLINTBEGIN(readability-identifier-naming): storage and descriptor contracts
  static checkpoint_type checkpoint() noexcept { return 0; }

  static void rewind(const checkpoint_type&) noexcept {}

  static std::size_t size() noexcept { return 0; }

  static std::string_view at(std::size_t) noexcept { return {}; }

  static bool try_emplace_back(std::string_view) noexcept { return false; }

  static void pop_back() noexcept {}

  // NOLINTEND(readability-identifier-naming)
};

static_assert(StringInternerIndex<DestructionBackend<true>, StringId<>>);
static_assert(StringInternerStorage<DestructionBackend<true>>);
static_assert(StringInternerEntries<DestructionBackend<true>>);
static_assert(!StringInternerIndex<DestructionBackend<false>, StringId<>>);
static_assert(!StringInternerStorage<DestructionBackend<false>>);
static_assert(!StringInternerEntries<DestructionBackend<false>>);
static_assert(std::is_nothrow_destructible_v<StringInterner<>>);

struct QueryCountingIndex final {
  explicit QueryCountingIndex(std::size_t& queries) noexcept : queries(queries) {}

  std::optional<StringId<>> find(std::string_view key) const noexcept {
    ++queries;
    return index.find(key);
  }

  std::optional<bool> try_insert(std::string_view key, StringId<> id) noexcept { return index.try_insert(key, id); }

  std::size_t& queries;
  HamtStringIndex<> index;
};

template<bool ParentFirst>
void CheckConfiguredSearchOrder() {
  using Inserted = std::pair<StringId<>, bool>;
  using Interner = StringInterner<
      std::uint32_t, ArenaStringStorage<>, mbo::container::SegmentedSequence<std::string_view>, QueryCountingIndex,
      StringInternerOptions{.parent_first = ParentFirst}>;
  std::size_t root_queries = 0;
  std::size_t child_queries = 0;
  Interner root(nullptr, [&]() noexcept { return QueryCountingIndex(root_queries); });
  EXPECT_THAT(root.intern("root").index(), Eq(0));
  Interner child(&root, [&]() noexcept { return QueryCountingIndex(child_queries); });
  EXPECT_THAT(child.intern("child").index(), Eq(0));
  root_queries = 0;
  child_queries = 0;
  EXPECT_THAT(child.intern("child"), VariantWith<Inserted>(Inserted(StringId<>(1), false)));
  EXPECT_THAT(child_queries, Eq(1));
  EXPECT_THAT(root_queries, Eq(ParentFirst ? 1 : 0));
  root_queries = 0;
  child_queries = 0;
  EXPECT_THAT(child.intern("root"), VariantWith<Inserted>(Inserted(StringId<>(0), false)));
  EXPECT_THAT(root_queries, Eq(1));
  EXPECT_THAT(child_queries, Eq(ParentFirst ? 0 : 1));
  root_queries = 0;
  child_queries = 0;
  EXPECT_THAT(child.intern_parent_first("child").index(), Eq(0));
  EXPECT_THAT(root_queries, Eq(1));
  EXPECT_THAT(child_queries, Eq(1));
  EXPECT_THAT(child.size(), Eq(2));
}

TEST_F(StringInternerTest, ParentFirstOptionQueriesTheParentBeforeLocalDuplicates) {
  CheckConfiguredSearchOrder<true>();
}

TEST_F(StringInternerTest, ChildFirstOptionAvoidsParentQueriesForLocalDuplicates) {
  CheckConfiguredSearchOrder<false>();
}

static_assert(std::bidirectional_iterator<StringInterner<>::iterator>);

TEST_F(StringInternerTest, InlineLimitedVectorDescriptorsSupportRecoverableCapacityExhaustion) {
  using Entries = mbo::container::LimitedVector<std::string_view, 1>;
  static_assert(StringInternerEntries<Entries>);
  StringInterner<std::uint32_t, ArenaStringStorage<>, Entries> interner;
  EXPECT_THAT(interner.try_intern_id("first"), Optional(StringId<>(0)));
  EXPECT_THAT(interner.try_intern_id("second").has_value(), Eq(false));
  EXPECT_THAT(interner.try_intern_id("first"), Optional(StringId<>(0)));
  EXPECT_THAT(interner.size(), Eq(1));
  EXPECT_THAT(interner.local_character_bytes_used(), Optional(std::size_t{5}));
  EXPECT_THAT(interner.get(StringId<>(0)), Optional(std::string_view("first")));
}

TEST_F(StringInternerTest, CallerOwnedBuffersAndInlineDescriptorsSupportBoundedCascades) {
  using Arena = mbo::memory::Arena<
      mbo::memory::InlineBlockSource<256>,
      mbo::memory::ArenaOptions{.initial_block_size = 128, .maximum_block_size = 128}>;
  using Storage = ArenaStringStorage<Arena>;
  using Entries = mbo::container::LimitedVector<std::string_view, 1>;
  using Index = HamtStringIndex<
      StringId<>, std::hash<std::string_view>, std::equal_to<>, mbo::container::HamtOptions{},
      mbo::memory::InlineBlockSource<4'096>>;
  using Interner = StringInterner<std::uint32_t, Storage, Entries, Index>;
  // Control storage contains the node source itself as well as domain metadata.
  mbo::memory::InlineBlockSource<8'192> root_control;
  mbo::memory::InlineBlockSource<8'192> child_control;
  const auto make_index = [](auto& control) noexcept {
    auto index = Index::try_create_in(control, std::hash<std::string_view>{}, std::equal_to<>{});
    if (!index) {
      std::terminate();
    }
    return std::move(*index);
  };
  Interner root(
      nullptr, []() noexcept { return Storage{}; }, []() noexcept { return Entries{}; },
      [&]() noexcept { return make_index(root_control); });
  EXPECT_THAT(root.try_intern_id("root"), Optional(StringId<>(0)));
  Interner child(
      &root, []() noexcept { return Storage{}; }, []() noexcept { return Entries{}; },
      [&]() noexcept { return make_index(child_control); });
  EXPECT_THAT(child.try_intern_id("child"), Optional(StringId<>(1)));
  EXPECT_THAT(child.try_intern_id("overflow").has_value(), Eq(false));
  EXPECT_THAT(child.try_intern_id("root"), Optional(StringId<>(0)));
  EXPECT_THAT(child.try_intern_id("child"), Optional(StringId<>(1)));
  EXPECT_THAT(child.size(), Eq(2));
  EXPECT_THAT(child.local_size(), Eq(1));
  EXPECT_THAT(child.local_character_bytes_used(), Optional(std::size_t{5}));
  EXPECT_THAT(child.get(StringId<>(0)), Optional(std::string_view("root")));
  EXPECT_THAT(child.get(StringId<>(1)), Optional(std::string_view("child")));
}

TEST_F(StringInternerTest, BoundedIndexExhaustionRollsBackBytesAndPreservesParentDuplicates) {
  using Index = HamtStringIndex<
      StringId<>, std::hash<std::string_view>, std::equal_to<>, mbo::container::HamtOptions{.maximum_size = 1}>;
  using Interner =
      StringInterner<std::uint32_t, ArenaStringStorage<>, mbo::container::SegmentedSequence<std::string_view>, Index>;
  Interner root;
  EXPECT_THAT(root.try_intern_id("root"), Optional(StringId<>(0)));
  Interner child(&root);
  EXPECT_THAT(child.try_intern_id("local"), Optional(StringId<>(1)));
  const auto original = child.get(StringId<>(1)).value_or(std::string_view{});
  const auto* const original_data = original.data();
  EXPECT_THAT(child.intern("overflow"), VariantWith<StringInternError>(StringInternError::kIndexExhausted));
  EXPECT_THAT(child.try_intern("overflow").has_value(), Eq(false));
  EXPECT_THAT(child.try_intern_id("overflow").has_value(), Eq(false));
  EXPECT_THAT(child.local_character_bytes_used(), Optional(std::size_t{5}));
  EXPECT_THAT(child.size(), Eq(2));
  EXPECT_THAT(child.local_size(), Eq(1));
  EXPECT_THAT(child.local_index_diagnostics().entries, Eq(1));
  EXPECT_THAT(child.find("overflow").has_value(), Eq(false));
  EXPECT_THAT(child.try_intern_id("root"), Optional(StringId<>(0)));
  EXPECT_THAT(child.try_intern_id("local"), Optional(StringId<>(1)));
  EXPECT_THAT(child.get(StringId<>(1)), Optional(std::string_view("local")));
  EXPECT_THAT(child.get(StringId<>(1)).value_or(std::string_view{}).data(), Eq(original_data));
  EXPECT_THAT(original, Eq("local"));
}

struct ConstantStringHash final {
  std::uint64_t operator()(std::string_view) const noexcept { return 7; }
};

TEST_F(StringInternerTest, CollisionBoundExhaustionRollsBackUnpublishedStrings) {
  using Index = HamtStringIndex<
      StringId<>, ConstantStringHash, std::equal_to<>, mbo::container::HamtOptions{.maximum_collision_size = 1}>;
  using Interner =
      StringInterner<std::uint32_t, ArenaStringStorage<>, mbo::container::SegmentedSequence<std::string_view>, Index>;
  Interner interner;
  EXPECT_THAT(interner.try_intern_id("first"), Optional(StringId<>(0)));
  const auto first = interner.get(StringId<>(0)).value_or(std::string_view{});
  const auto* const first_data = first.data();
  EXPECT_THAT(interner.intern("second"), VariantWith<StringInternError>(StringInternError::kIndexExhausted));
  EXPECT_THAT(interner.size(), Eq(1));
  EXPECT_THAT(interner.local_size(), Eq(1));
  EXPECT_THAT(interner.local_character_bytes_used(), Optional(std::size_t{5}));
  EXPECT_THAT(interner.find("second").has_value(), Eq(false));
  EXPECT_THAT(interner.try_intern_id("first"), Optional(StringId<>(0)));
  EXPECT_THAT(interner.get(StringId<>(0)).value_or(std::string_view{}).data(), Eq(first_data));
  EXPECT_THAT(first, Eq("first"));
}

TEST_F(StringInternerTest, LocalIndexDiagnosticsDoNotIncludeParentOrPostCutoffEntries) {
  StringInterner<> root;
  EXPECT_THAT(root.intern("shared").index(), Eq(0));
  StringInterner<> child(&root);
  EXPECT_THAT(root.intern("late").index(), Eq(0));
  EXPECT_THAT(child.intern("local").index(), Eq(0));
  EXPECT_THAT(root.local_index_diagnostics().entries, Eq(2));
  EXPECT_THAT(child.local_index_diagnostics().entries, Eq(1));
  std::size_t index_entries = 0;
  child.visit_local_index_nodes([&](const auto& node) noexcept { index_entries += node.entries; });
  EXPECT_THAT(index_entries, Eq(1));
  EXPECT_THAT(child.size(), Eq(2));
  EXPECT_THAT(child.find("late").has_value(), Eq(false));
  EXPECT_THAT(child.find("shared"), Optional(StringId<>(0)));
  EXPECT_THAT(child.find("local"), Optional(StringId<>(1)));
}

TEST_F(StringInternerTest, EntryStorageDiagnosticsSeparateLiveDescriptorsFromReservedMemory) {
  StringInterner<> root;
  EXPECT_THAT(root.intern("root").index(), Eq(0));
  StringInterner<> child(&root);
  const auto empty = child.local_entry_storage_diagnostics();
  EXPECT_THAT(empty.descriptors, Eq(0));
  EXPECT_THAT(empty.live_descriptor_bytes, Eq(0));
  EXPECT_THAT(empty.segment_bytes_reserved, Optional(std::size_t{0}));
  EXPECT_THAT(child.intern("local").index(), Eq(0));
  const auto measured = child.local_entry_storage_diagnostics();
  EXPECT_THAT(measured.descriptors, Eq(1));
  EXPECT_THAT(measured.live_descriptor_bytes, Eq(sizeof(std::string_view)));
  ASSERT_THAT(measured.segment_bytes_reserved.has_value(), Eq(true));
  EXPECT_THAT(measured.segment_bytes_reserved.value() >= measured.live_descriptor_bytes, Eq(true));
  EXPECT_THAT(measured.lookup_directory_bytes_reserved.has_value(), Eq(true));
  EXPECT_THAT(measured.segment_directory_bytes_reserved.has_value(), Eq(true));
  EXPECT_THAT(child.size(), Eq(2));
}

TEST_F(StringInternerTest, StorageDiagnosticsCombineEveryLocalStorageLayer) {
  StringInterner<> root;
  EXPECT_THAT(root.intern("root").index(), Eq(0));
  StringInterner<> child(&root);
  EXPECT_THAT(child.intern("local").index(), Eq(0));

  const auto measured = child.local_storage_diagnostics();
  EXPECT_THAT(measured.string_count, Eq(1));
  EXPECT_THAT(measured.character_bytes_used, Optional(std::size_t{5}));
  EXPECT_THAT(measured.character_bytes_reserved.has_value(), Eq(true));
  EXPECT_THAT(measured.live_descriptor_bytes, Eq(sizeof(std::string_view)));
  EXPECT_THAT(measured.descriptor_segment_bytes_reserved.has_value(), Eq(true));
  EXPECT_THAT(measured.descriptor_lookup_directory_bytes_reserved.has_value(), Eq(true));
  EXPECT_THAT(measured.descriptor_segment_directory_bytes_reserved.has_value(), Eq(true));
  EXPECT_THAT(measured.index_nodes.has_value(), Eq(true));
  EXPECT_THAT(measured.index_entries, Optional(std::size_t{1}));
  EXPECT_THAT(measured.index_collision_nodes, Optional(std::size_t{0}));
  EXPECT_THAT(measured.index_collision_entries, Optional(std::size_t{0}));
  EXPECT_THAT(measured.index_largest_collision, Optional(std::size_t{0}));
  EXPECT_THAT(measured.index_maximum_depth, Optional(std::size_t{0}));
  EXPECT_THAT(measured.index_node_bytes.has_value(), Eq(true));
  EXPECT_THAT(measured.index_entry_bytes, Optional(std::size_t{0}));
}

struct UnmeasuredEntries final {
  using value_type = std::string_view;

  std::size_t size() const noexcept { return entries.size(); }

  std::string_view at(std::size_t index) const noexcept { return entries.at(index); }

  bool try_emplace_back(std::string_view value) noexcept { return entries.try_emplace_back(value).has_value(); }

  void pop_back() noexcept { entries.pop_back(); }

  mbo::container::SegmentedSequence<std::string_view> entries;
};

static_assert(StringInternerEntries<UnmeasuredEntries>);
static_assert(StringInternerEntries<mbo::container::SegmentedSequence<std::string_view>>);
static_assert(!StringInternerEntries<mbo::container::SegmentedSequence<int>>);
static_assert(!StringInternerEntries<std::array<std::string_view, 1>>);

TEST_F(StringInternerTest, UnsupportedEntryMemoryStatisticsRemainUnknown) {
  StringInterner<std::uint32_t, ArenaStringStorage<>, UnmeasuredEntries> interner;
  EXPECT_THAT(interner.intern("owned").index(), Eq(0));
  const auto measured = interner.local_entry_storage_diagnostics();
  EXPECT_THAT(measured.descriptors, Eq(1));
  EXPECT_THAT(measured.live_descriptor_bytes, Eq(sizeof(std::string_view)));
  EXPECT_THAT(measured.segment_bytes_reserved.has_value(), Eq(false));
  EXPECT_THAT(measured.lookup_directory_bytes_reserved.has_value(), Eq(false));
  EXPECT_THAT(measured.segment_directory_bytes_reserved.has_value(), Eq(false));
  const auto combined = interner.local_storage_diagnostics();
  EXPECT_THAT(combined.descriptor_segment_bytes_reserved.has_value(), Eq(false));
  EXPECT_THAT(combined.descriptor_lookup_directory_bytes_reserved.has_value(), Eq(false));
  EXPECT_THAT(combined.descriptor_segment_directory_bytes_reserved.has_value(), Eq(false));
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

TEST_F(StringInternerTest, FiniteParentDepthRejectsAnUnboundedCascade) {
  using Bounded = StringInterner<
      std::uint32_t, ArenaStringStorage<>, mbo::container::SegmentedSequence<std::string_view>,
      HamtStringIndex<StringId<>>, StringInternerOptions{.maximum_parent_depth = 1}>;
  static_assert(Bounded::max_parent_depth() == 1);
  Bounded root;
  Bounded child(&root);
  EXPECT_DEATH(static_cast<void>(Bounded(&child)), "parent chain exceeds maximum_parent_depth=1");
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
  EXPECT_THAT(bounded.try_intern("x").has_value(), Eq(false));
  EXPECT_THAT(bounded.try_intern_id("x").has_value(), Eq(false));
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
  EXPECT_THAT(one.try_intern("second").has_value(), Eq(false));
  EXPECT_THAT(one.try_intern_id("second").has_value(), Eq(false));
  EXPECT_THAT(one.try_intern_id("first"), Optional(StringId<>(0)));
  EXPECT_THAT(one.size(), Eq(1));
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
  static_assert(StringInterner<std::uint8_t>::max_size() == 255);
  static_assert(StringInterner<std::uint64_t>::max_size() == std::numeric_limits<std::size_t>::max());
  for (unsigned value = 0; value < StringInterner<std::uint8_t>::max_size(); ++value) {
    const auto text = std::to_string(value);
    EXPECT_THAT(interner.intern(text).index(), Eq(0));
  }
  EXPECT_THAT(interner.size(), Eq(255));
  EXPECT_THAT(interner.get(StringId<std::uint8_t>(254)), Optional(std::string_view("254")));
  EXPECT_THAT(interner.get(StringId<std::uint8_t>{}), Eq(std::nullopt));
  EXPECT_THAT(interner.try_intern("exhausted").has_value(), Eq(false));
  EXPECT_THAT(interner.try_intern_id("exhausted").has_value(), Eq(false));
  EXPECT_THAT(interner.try_intern("0"), Optional(std::pair(StringId<std::uint8_t>(0), false)));
  EXPECT_THAT(interner.try_intern_id("254"), Optional(StringId<std::uint8_t>(254)));
  EXPECT_THAT(interner.intern("exhausted"), VariantWith<StringInternError>(StringInternError::kIdExhausted));
  EXPECT_THAT(interner.intern("0").index(), Eq(0));
  EXPECT_THAT(interner.size(), Eq(255));
}

TEST_F(StringInternerTest, OptionalInsertionAdaptersPreserveDenseIdsAndParentDuplicates) {
  StringInterner<> root;
  EXPECT_THAT(root.try_intern("root"), Optional(std::pair(StringId<>(0), true)));
  StringInterner<> child(&root);
  EXPECT_THAT(child.try_intern("root"), Optional(std::pair(StringId<>(0), false)));
  EXPECT_THAT(child.try_intern_id("local"), Optional(StringId<>(1)));
  EXPECT_THAT(child.try_intern("local"), Optional(std::pair(StringId<>(1), false)));
  EXPECT_THAT(child.size(), Eq(2));
  EXPECT_THAT(child.local_size(), Eq(1));
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
