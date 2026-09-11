// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_insert.h"

#include <cstdint>
#include <limits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
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

struct HamtInsertTest : ::testing::Test {
  mbo::memory::NewDeleteBlockSource source;

  std::optional<HamtInsertResult<Node>> Insert(Node* root, std::uint64_t hash, int key) {
    return TryInsertHamtEntry<std::uint64_t, 5>(
        source, root, hash, key, Entry{.hash = hash, .key = key}, HashOf{}, KeyOf{}, Equal{});
  }

  const Entry* Find(Node* root, std::uint64_t hash, int key) {
    return FindHamtEntry(root, hash, key, HashOf{}, KeyOf{}, Equal{});
  }
};

TEST_F(HamtInsertTest, InsertsIntoEmptyRootAndPreservesTheOriginalVersion) {
  auto first = Insert(nullptr, 1, 10);
  ASSERT_THAT(first, Optional(_));
  auto second = Insert(first->root, 2, 20);
  ASSERT_THAT(second, Optional(_));

  EXPECT_THAT(first->inserted, Eq(true));
  EXPECT_THAT(second->inserted, Eq(true));
  EXPECT_THAT(Find(first->root, 1, 10), NotNull());
  EXPECT_THAT(Find(first->root, 2, 20), IsNull());
  EXPECT_THAT(Find(second->root, 1, 10), NotNull());
  EXPECT_THAT(Find(second->root, 2, 20), NotNull());
  Node::Release(source, first->root);
  Node::Release(source, second->root);
}

TEST_F(HamtInsertTest, BuildsAndCopiesDeepBranches) {
  constexpr std::uint64_t kFirstHash = 3;
  constexpr std::uint64_t kSecondHash = 3 + (std::uint64_t{7} << 15);
  constexpr std::uint64_t kThirdHash = 3 + (std::uint64_t{9} << 15);
  auto first = Insert(nullptr, kFirstHash, 10);
  ASSERT_THAT(first, Optional(_));
  auto second = Insert(first->root, kSecondHash, 20);
  ASSERT_THAT(second, Optional(_));
  auto third = Insert(second->root, kThirdHash, 30);
  ASSERT_THAT(third, Optional(_));

  EXPECT_THAT(Find(second->root, kThirdHash, 30), IsNull());
  EXPECT_THAT(Find(third->root, kFirstHash, 10), NotNull());
  EXPECT_THAT(Find(third->root, kSecondHash, 20), NotNull());
  EXPECT_THAT(Find(third->root, kThirdHash, 30), NotNull());
  Node::Release(source, first->root);
  Node::Release(source, second->root);
  Node::Release(source, third->root);
}

TEST_F(HamtInsertTest, ExtendsFullHashCollisionsAndRejectsDuplicateKeys) {
  auto first = Insert(nullptr, 7, 10);
  ASSERT_THAT(first, Optional(_));
  auto second = Insert(first->root, 7, 20);
  ASSERT_THAT(second, Optional(_));
  auto duplicate = Insert(second->root, 7, 20);
  ASSERT_THAT(duplicate, Optional(_));

  EXPECT_THAT(second->root->children()[0]->is_collision(), Eq(true));
  EXPECT_THAT(duplicate->inserted, Eq(false));
  EXPECT_THAT(duplicate->root, Eq(second->root));
  EXPECT_THAT(Find(duplicate->root, 7, 10), NotNull());
  EXPECT_THAT(Find(duplicate->root, 7, 20), NotNull());
  Node::Release(source, first->root);
  Node::Release(source, second->root);
  Node::Release(source, duplicate->root);
}

TEST_F(HamtInsertTest, ReportsAllocationFailureWithoutChangingTheOriginal) {
  auto first = Insert(nullptr, 1, 10);
  ASSERT_THAT(first, Optional(_));
  mbo::memory::InlineBlockSource<1> exhausted;
  const auto failed = TryInsertHamtEntry<std::uint64_t, 5>(
      exhausted, first->root, 2ULL, 20, Entry{.hash = 2, .key = 20}, HashOf{}, KeyOf{}, Equal{});

  EXPECT_THAT(failed, Eq(std::nullopt));
  EXPECT_THAT(Find(first->root, 1, 10), NotNull());
  EXPECT_THAT(Find(first->root, 2, 20), IsNull());
  Node::Release(source, first->root);
}

}  // namespace
}  // namespace mbo::container::container_internal
