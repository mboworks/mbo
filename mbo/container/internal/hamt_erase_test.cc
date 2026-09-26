// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_erase.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/internal/hamt_insert.h"
#include "mbo/container/internal/hamt_lookup.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::SizeIs;

struct Entry final {
  std::uint64_t hash;
  int key;
};

struct HashOf final {
  constexpr std::uint64_t operator()(const Entry& entry) const noexcept { return entry.hash; }
};

struct KeyOf final {
  constexpr int operator()(const Entry& entry) const noexcept { return entry.key; }
};

struct Equal final {
  constexpr bool operator()(int lhs, int rhs) const noexcept { return lhs == rhs; }
};

using Node = HamtSharedNode<5, Entry>;

struct HamtEraseTest : ::testing::Test {
  mbo::memory::NewDeleteBlockSource source;

  HamtInsertResult<Node> Insert(Node* root, std::uint64_t hash, int key) {
    return TryInsertHamtEntry<std::uint64_t, 5>(
               source, root, hash, key, Entry{.hash = hash, .key = key}, HashOf{}, KeyOf{}, Equal{})
        .value_or(HamtInsertResult<Node>{.root = nullptr, .inserted = false});
  }

  HamtEraseResult<Node> Erase(Node* root, std::uint64_t hash, int key) {
    return TryEraseHamtEntry<std::uint64_t, 5>(source, root, hash, key, HashOf{}, KeyOf{}, Equal{})
        .value_or(HamtEraseResult<Node>{.root = nullptr, .erased = false});
  }

  static const Entry* Find(Node* root, std::uint64_t hash, int key) {
    return FindHamtEntry(root, hash, key, HashOf{}, KeyOf{}, Equal{});
  }
};

TEST_F(HamtEraseTest, ErasesLeafAndPreservesOriginalVersion) {
  auto first = Insert(nullptr, 1, 10);
  ASSERT_THAT(first.root, NotNull());
  auto second = Insert(first.root, 2, 20);
  ASSERT_THAT(second.root, NotNull());
  auto erased = Erase(second.root, 1, 10);
  ASSERT_THAT(erased.erased, Eq(true));

  EXPECT_THAT(erased.erased, Eq(true));
  EXPECT_THAT(Find(erased.root, 1, 10), IsNull());
  EXPECT_THAT(Find(erased.root, 2, 20), NotNull());
  EXPECT_THAT(Find(second.root, 1, 10), NotNull());
  Node::Release(source, first.root);
  Node::Release(source, second.root);
  Node::Release(source, erased.root);
}

TEST_F(HamtEraseTest, CollapsesDeepBranchesAfterErasingTheirLeaf) {
  constexpr std::uint64_t kFirstHash = 3;
  constexpr std::uint64_t kSecondHash = 3 + (std::uint64_t{7} << 15);
  auto first = Insert(nullptr, kFirstHash, 10);
  ASSERT_THAT(first.root, NotNull());
  auto second = Insert(first.root, kSecondHash, 20);
  ASSERT_THAT(second.root, NotNull());
  auto erased = Erase(second.root, kSecondHash, 20);
  ASSERT_THAT(erased.erased, Eq(true));

  ASSERT_THAT(erased.root, NotNull());
  EXPECT_THAT(erased.root->children(), IsEmpty());
  EXPECT_THAT(erased.root->entries(), SizeIs(1));
  EXPECT_THAT(Find(erased.root, kFirstHash, 10), NotNull());
  Node::Release(source, first.root);
  Node::Release(source, second.root);
  Node::Release(source, erased.root);
}

TEST_F(HamtEraseTest, CollapsesTwoEntryCollisionToARegularLeaf) {
  auto first = Insert(nullptr, 7, 10);
  ASSERT_THAT(first.root, NotNull());
  auto second = Insert(first.root, 7, 20);
  ASSERT_THAT(second.root, NotNull());
  auto erased = Erase(second.root, 7, 10);
  ASSERT_THAT(erased.erased, Eq(true));

  ASSERT_THAT(erased.root, NotNull());
  EXPECT_THAT(erased.root->children(), IsEmpty());
  EXPECT_THAT(erased.root->entries(), SizeIs(1));
  EXPECT_THAT(Find(erased.root, 7, 10), IsNull());
  EXPECT_THAT(Find(erased.root, 7, 20), NotNull());
  Node::Release(source, first.root);
  Node::Release(source, second.root);
  Node::Release(source, erased.root);
}

TEST_F(HamtEraseTest, MissingKeyReturnsAnotherReferenceToTheSameVersion) {
  auto first = Insert(nullptr, 1, 10);
  ASSERT_THAT(first.root, NotNull());
  auto missing = Erase(first.root, 2, 20);
  ASSERT_THAT(missing.root, NotNull());

  EXPECT_THAT(missing.erased, Eq(false));
  EXPECT_THAT(missing.root, Eq(first.root));
  EXPECT_THAT(first.root->use_count(), Eq(2));
  Node::Release(source, first.root);
  Node::Release(source, missing.root);
}

TEST_F(HamtEraseTest, ErasesTheLastEntryToAnEmptyRoot) {
  auto first = Insert(nullptr, 1, 10);
  ASSERT_THAT(first.root, NotNull());
  auto erased = Erase(first.root, 1, 10);
  ASSERT_THAT(erased.erased, Eq(true));

  EXPECT_THAT(erased.erased, Eq(true));
  EXPECT_THAT(erased.root, IsNull());
  Node::Release(source, first.root);
}

TEST_F(HamtEraseTest, ErasesAChildThatBecomesEmpty) {
  constexpr std::uint64_t kHash = 1;
  constexpr auto kEntries = std::to_array<Entry>({Entry{.hash = kHash, .key = 10}});
  Node::index_type child_index;
  ASSERT_THAT(child_index.InsertData(0), Eq(true));
  auto* const child = Node::TryCreate(source, child_index, kEntries, {}).value_or(nullptr);
  ASSERT_THAT(child, NotNull());

  Node::index_type root_index;
  ASSERT_THAT(root_index.InsertNode(1), Eq(true));
  const auto children = std::to_array<Node*>({child});
  auto* const root = Node::TryCreate(source, root_index, {}, children).value_or(nullptr);
  ASSERT_THAT(root, NotNull());

  const auto erased = Erase(root, kHash, 10);
  EXPECT_THAT(erased.erased, Eq(true));
  EXPECT_THAT(erased.root, IsNull());
  EXPECT_THAT(Find(root, kHash, 10), NotNull());
  Node::Release(source, root);
  Node::Release(source, child);
}

TEST_F(HamtEraseTest, ErasesASingleEntryCollisionToAnEmptyRoot) {
  constexpr auto kEntries = std::to_array<Entry>({Entry{.hash = 7, .key = 10}});
  auto* const original = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  const auto erased = Erase(original, 7, 10);
  ASSERT_THAT(erased.erased, Eq(true));
  EXPECT_THAT(erased.erased, Eq(true));
  EXPECT_THAT(erased.root, IsNull());
  EXPECT_THAT(Find(original, 7, 10), NotNull());
  Node::Release(source, original);
}

TEST_F(HamtEraseTest, MixedHashCollisionDemotionAndReinsertionPreserveEverySnapshot) {
  const auto first = Insert(nullptr, 7, 10);
  ASSERT_THAT(first.root, NotNull());
  const auto second = Insert(first.root, 7, 20);
  ASSERT_THAT(second.root, NotNull());
  constexpr std::uint64_t kNewHash = 7 + (std::uint64_t{7} << 15);
  const auto split = Insert(second.root, kNewHash, 30);
  ASSERT_THAT(split.root, NotNull());
  const auto erased = Erase(split.root, 7, 10);
  ASSERT_THAT(erased.erased, Eq(true));
  EXPECT_THAT(Find(split.root, 7, 10), NotNull());
  EXPECT_THAT(Find(erased.root, 7, 10), IsNull());
  EXPECT_THAT(Find(erased.root, 7, 20), NotNull());
  EXPECT_THAT(Find(erased.root, kNewHash, 30), NotNull());
  const auto reinserted = Insert(erased.root, 7, 40);
  ASSERT_THAT(reinserted.root, NotNull());
  EXPECT_THAT(Find(erased.root, 7, 40), IsNull());
  const auto compacted = Erase(reinserted.root, kNewHash, 30);
  ASSERT_THAT(compacted.erased, Eq(true));
  Node::Release(source, first.root);
  Node::Release(source, second.root);
  Node::Release(source, split.root);
  Node::Release(source, erased.root);
  Node::Release(source, reinserted.root);
  EXPECT_THAT(Find(compacted.root, 7, 20), NotNull());
  EXPECT_THAT(Find(compacted.root, 7, 40), NotNull());
  EXPECT_THAT(Find(compacted.root, kNewHash, 30), IsNull());
  Node::Release(source, compacted.root);
}

TEST_F(HamtEraseTest, EmptyRootIsASuccessfulMissingKeyWithoutAllocation) {
  mbo::memory::InlineBlockSource<1> exhausted;
  const auto missing = TryEraseHamtEntry<std::uint64_t, 5>(
      exhausted, static_cast<Node*>(nullptr), std::uint64_t{1}, 10, HashOf{}, KeyOf{}, Equal{});
  EXPECT_THAT(missing, Optional(Field("root", &HamtEraseResult<Node>::root, IsNull())));
  EXPECT_THAT(missing, Optional(Field("erased", &HamtEraseResult<Node>::erased, Eq(false))));
}

TEST_F(HamtEraseTest, CompactPreservesAnEmptyRootWithoutAllocation) {
  using Step = hamt_erase_internal::HamtEraseStep<Node, Entry>;
  const auto compacted = hamt_erase_internal::Compact<Node, Entry>(source, nullptr);
  EXPECT_THAT(compacted, Optional(Field("node", &Step::node, IsNull())));
  EXPECT_THAT(compacted, Optional(Field("erased", &Step::erased, Eq(true))));
}

TEST_F(HamtEraseTest, CompactReleasesEmptyBitmapNodesAndPreservesCollisionNodes) {
  using Step = hamt_erase_internal::HamtEraseStep<Node, Entry>;
  auto* const empty = Node::TryCreate(source, {}, std::span<const Entry>{}, std::span<Node* const>{}).value_or(nullptr);
  ASSERT_THAT(empty, NotNull());
  const auto compacted_empty = hamt_erase_internal::Compact<Node, Entry>(source, empty);
  EXPECT_THAT(compacted_empty, Optional(Field("node", &Step::node, IsNull())));
  EXPECT_THAT(compacted_empty, Optional(Field("erased", &Step::erased, Eq(true))));

  constexpr auto kEntries = std::to_array<Entry>({Entry{.hash = 7, .key = 10}});
  auto* const collision = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(collision, NotNull());
  const auto preserved = hamt_erase_internal::Compact<Node, Entry>(source, collision);
  ASSERT_THAT(preserved, Optional(Field("node", &Step::node, NotNull())));
  const Step* const preserved_step = preserved ? std::addressof(*preserved) : nullptr;
  ASSERT_THAT(preserved_step, NotNull());
  EXPECT_THAT(preserved_step->node, Eq(collision));
  Node::Release(source, preserved_step->node);
}

TEST_F(HamtEraseTest, ErasurePastTheCompleteHashPathPreservesTheOriginal) {
  auto first = Insert(nullptr, 1, 10);
  ASSERT_THAT(first.root, NotNull());
  constexpr std::size_t kPastHashPath = HamtHashPath<std::uint64_t, 5>::kLevels;
  const auto missing = hamt_erase_internal::TryEraseAt<std::uint64_t, 5>(
      source, first.root, std::uint64_t{2}, 20, kPastHashPath, HashOf{}, KeyOf{}, Equal{});
  using Step = hamt_erase_internal::HamtEraseStep<Node, Entry>;
  ASSERT_THAT(missing, Optional(Field("node", &Step::node, NotNull())));
  EXPECT_THAT(missing, Optional(Field("erased", &Step::erased, Eq(false))));
  const Step* const missing_step = missing ? std::addressof(*missing) : nullptr;
  ASSERT_THAT(missing_step, NotNull());
  EXPECT_THAT(missing_step->node, Eq(first.root));
  EXPECT_THAT(first.root->use_count(), Eq(2));
  Node::Release(source, missing_step->node);
  Node::Release(source, first.root);
}

TEST_F(HamtEraseTest, AllocationFailurePreservesTheOriginalSnapshot) {
  const auto first = Insert(nullptr, 1, 10);
  ASSERT_THAT(first.root, NotNull());
  const auto second = Insert(first.root, 2, 20);
  ASSERT_THAT(second.root, NotNull());
  mbo::memory::InlineBlockSource<1> exhausted;
  const auto failed =
      TryEraseHamtEntry<std::uint64_t, 5>(exhausted, second.root, std::uint64_t{1}, 10, HashOf{}, KeyOf{}, Equal{});
  EXPECT_THAT(failed, Eq(std::nullopt));
  EXPECT_THAT(Find(second.root, 1, 10), NotNull());
  EXPECT_THAT(Find(second.root, 2, 20), NotNull());
  EXPECT_THAT(second.root->use_count(), Eq(1));
  Node::Release(source, first.root);
  Node::Release(source, second.root);
}

struct BudgetSource final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    if (remaining == 0) {
      return std::nullopt;
    }
    auto block = mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
    if (block) {
      --remaining;
      ++acquired;
    }
    return block;
  }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    ++released;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }

  std::size_t remaining = 0;
  std::size_t acquired = 0;
  std::size_t released = 0;
};

TEST_F(HamtEraseTest, CollisionPathCopyBudgetsPreserveSnapshotsAndReclaimTemporaryNodes) {
  const auto first = Insert(nullptr, 7, 10);
  ASSERT_THAT(first.root, NotNull());
  const auto second = Insert(first.root, 7, 20);
  ASSERT_THAT(second.root, NotNull());
  const auto third = Insert(second.root, 7, 30);
  ASSERT_THAT(third.root, NotNull());
  const auto split = Insert(third.root, 39, 40);
  ASSERT_THAT(split.root, NotNull());
  for (auto* const original : std::to_array<Node*>({third.root, split.root})) {
    for (std::size_t allowed = 0; allowed <= 5; ++allowed) {
      SCOPED_TRACE(allowed);
      BudgetSource budget{.remaining = allowed};
      const auto result =
          TryEraseHamtEntry<std::uint64_t, 5>(budget, original, std::uint64_t{7}, 30, HashOf{}, KeyOf{}, Equal{});
      if (result) {
        EXPECT_THAT(result->erased, Eq(true));
        EXPECT_THAT(Find(result->root, 7, 30), IsNull());
        EXPECT_THAT(Find(result->root, 7, 10), NotNull());
        EXPECT_THAT(Find(result->root, 7, 20), NotNull());
        Node::Release(budget, result->root);
      }
      if (allowed == 0) {
        EXPECT_THAT(result.has_value(), Eq(false));
      } else if (allowed == 5) {
        EXPECT_THAT(result.has_value(), Eq(true));
      }
      EXPECT_THAT(Find(original, 7, 30), NotNull());
      EXPECT_THAT(original->use_count(), Eq(1));
      EXPECT_THAT(budget.released, Eq(budget.acquired));
    }
  }
  Node::Release(source, first.root);
  Node::Release(source, second.root);
  Node::Release(source, third.root);
  Node::Release(source, split.root);
}

TEST_F(HamtEraseTest, MissingHashesAndKeysPreserveLeafCollisionAndNestedVersions) {
  const auto leaf = Insert(nullptr, 7, 10);
  ASSERT_THAT(leaf.root, NotNull());
  const auto nested = Insert(leaf.root, 7, 20);
  ASSERT_THAT(nested.root, NotNull());
  constexpr auto kEntries = std::to_array<Entry>({Entry{.hash = 7, .key = 10}, Entry{.hash = 7, .key = 20}});
  auto* const collision = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(collision, NotNull());
  for (auto* const original : std::to_array<Node*>({leaf.root, nested.root, collision})) {
    for (const std::uint64_t hash : std::to_array<std::uint64_t>({7, 39})) {
      const auto missing = Erase(original, hash, 99);
      EXPECT_THAT(missing.root, Eq(original));
      EXPECT_THAT(missing.erased, Eq(false));
      Node::Release(source, missing.root);
    }
  }
  const auto erased = Erase(collision, 7, 20);
  ASSERT_THAT(erased.root, NotNull());
  EXPECT_THAT(erased.erased, Eq(true));
  EXPECT_THAT(Find(erased.root, 7, 10), NotNull());
  EXPECT_THAT(Find(erased.root, 7, 20), IsNull());
  Node::Release(source, erased.root);
  BudgetSource exhausted;
  EXPECT_THAT(
      (TryEraseHamtEntry<std::uint64_t, 5>(exhausted, collision, std::uint64_t{7}, 20, HashOf{}, KeyOf{}, Equal{})),
      Eq(std::nullopt));
  EXPECT_THAT(Find(collision, 7, 20), NotNull());
  Node::Release(source, leaf.root);
  Node::Release(source, nested.root);
  Node::Release(source, collision);
}

}  // namespace
}  // namespace mbo::container::container_internal
