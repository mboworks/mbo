// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_erase.h"

#include <array>
#include <cstdint>
#include <optional>

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

  const Entry* Find(Node* root, std::uint64_t hash, int key) {
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

}  // namespace
}  // namespace mbo::container::container_internal
