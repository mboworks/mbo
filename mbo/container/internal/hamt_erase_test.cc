// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_erase.h"

#include <cstdint>
#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/internal/hamt_insert.h"
#include "mbo/container/internal/hamt_lookup.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Optional;

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

  std::optional<HamtInsertResult<Node>> Insert(Node* root, std::uint64_t hash, int key) {
    return TryInsertHamtEntry<std::uint64_t, 5>(
        source, root, hash, key, Entry{.hash = hash, .key = key}, HashOf{}, KeyOf{}, Equal{});
  }

  std::optional<HamtEraseResult<Node>> Erase(Node* root, std::uint64_t hash, int key) {
    return TryEraseHamtEntry<std::uint64_t, 5>(source, root, hash, key, HashOf{}, KeyOf{}, Equal{});
  }

  const Entry* Find(Node* root, std::uint64_t hash, int key) {
    return FindHamtEntry(root, hash, key, HashOf{}, KeyOf{}, Equal{});
  }
};

TEST_F(HamtEraseTest, ErasesLeafAndPreservesOriginalVersion) {
  auto first = Insert(nullptr, 1, 10);
  ASSERT_THAT(first, Optional(_));
  auto second = Insert(first->root, 2, 20);
  ASSERT_THAT(second, Optional(_));
  auto erased = Erase(second->root, 1, 10);
  ASSERT_THAT(erased, Optional(_));

  EXPECT_THAT(erased->erased, Eq(true));
  EXPECT_THAT(Find(erased->root, 1, 10), IsNull());
  EXPECT_THAT(Find(erased->root, 2, 20), NotNull());
  EXPECT_THAT(Find(second->root, 1, 10), NotNull());
  Node::Release(source, first->root);
  Node::Release(source, second->root);
  Node::Release(source, erased->root);
}

TEST_F(HamtEraseTest, CollapsesDeepBranchesAfterErasingTheirLeaf) {
  constexpr std::uint64_t kFirstHash = 3;
  constexpr std::uint64_t kSecondHash = 3 + (std::uint64_t{7} << 15);
  auto first = Insert(nullptr, kFirstHash, 10);
  ASSERT_THAT(first, Optional(_));
  auto second = Insert(first->root, kSecondHash, 20);
  ASSERT_THAT(second, Optional(_));
  auto erased = Erase(second->root, kSecondHash, 20);
  ASSERT_THAT(erased, Optional(_));

  EXPECT_THAT(erased->root->children().empty(), Eq(true));
  EXPECT_THAT(erased->root->entries().size(), Eq(1));
  EXPECT_THAT(Find(erased->root, kFirstHash, 10), NotNull());
  Node::Release(source, first->root);
  Node::Release(source, second->root);
  Node::Release(source, erased->root);
}

TEST_F(HamtEraseTest, CollapsesTwoEntryCollisionToARegularLeaf) {
  auto first = Insert(nullptr, 7, 10);
  ASSERT_THAT(first, Optional(_));
  auto second = Insert(first->root, 7, 20);
  ASSERT_THAT(second, Optional(_));
  auto erased = Erase(second->root, 7, 10);
  ASSERT_THAT(erased, Optional(_));

  EXPECT_THAT(erased->root->children().empty(), Eq(true));
  EXPECT_THAT(erased->root->entries().size(), Eq(1));
  EXPECT_THAT(Find(erased->root, 7, 10), IsNull());
  EXPECT_THAT(Find(erased->root, 7, 20), NotNull());
  Node::Release(source, first->root);
  Node::Release(source, second->root);
  Node::Release(source, erased->root);
}

TEST_F(HamtEraseTest, MissingKeyReturnsAnotherReferenceToTheSameVersion) {
  auto first = Insert(nullptr, 1, 10);
  ASSERT_THAT(first, Optional(_));
  auto missing = Erase(first->root, 2, 20);
  ASSERT_THAT(missing, Optional(_));

  EXPECT_THAT(missing->erased, Eq(false));
  EXPECT_THAT(missing->root, Eq(first->root));
  EXPECT_THAT(first->root->use_count(), Eq(2));
  Node::Release(source, first->root);
  Node::Release(source, missing->root);
}

TEST_F(HamtEraseTest, ErasesTheLastEntryToAnEmptyRoot) {
  auto first = Insert(nullptr, 1, 10);
  ASSERT_THAT(first, Optional(_));
  auto erased = Erase(first->root, 1, 10);
  ASSERT_THAT(erased, Optional(_));

  EXPECT_THAT(erased->erased, Eq(true));
  EXPECT_THAT(erased->root, IsNull());
  Node::Release(source, first->root);
}

}  // namespace
}  // namespace mbo::container::container_internal
