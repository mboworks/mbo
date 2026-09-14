// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_replace.h"

#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/internal/hamt_insert.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::Eq;
using ::testing::Field;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Optional;

struct Entry final {
  std::uint64_t hash;
  int key;
  int value;
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

struct HamtReplaceTest : ::testing::Test {
  void TearDown() override { Node::Release(source, root); }

  void Insert(Entry entry) {
    const auto inserted = TryInsertHamtEntry(source, root, entry.hash, entry.key, entry, HashOf{}, KeyOf{}, Equal{})
                              .value_or({.root = nullptr, .inserted = false});
    ASSERT_THAT(inserted.root, NotNull());
    Node::Release(source, root);
    root = inserted.root;
  }

  const Entry* Find(const Node* node, std::uint64_t hash, int key) {
    return FindHamtEntry(node, hash, key, HashOf{}, KeyOf{}, Equal{});
  }

  mbo::memory::NewDeleteBlockSource source;
  Node* root = nullptr;
};

TEST_F(HamtReplaceTest, NullRootAndMissingKeysSucceedWithoutAllocation) {
  mbo::memory::InlineBlockSource<1> exhausted;
  constexpr Entry kReplacement{.hash = 1, .key = 10, .value = 99};
  const auto empty = TryReplaceHamtEntry(
                         exhausted, root, kReplacement.hash, kReplacement.key, kReplacement, HashOf{}, KeyOf{}, Equal{})
                         .value_or({.root = nullptr, .replaced = true});
  EXPECT_THAT(empty.root, IsNull());
  EXPECT_THAT(empty.replaced, Eq(false));
  Insert(Entry{.hash = 2, .key = 20, .value = 20});
  const auto missing =
      TryReplaceHamtEntry(
          exhausted, root, kReplacement.hash, kReplacement.key, kReplacement, HashOf{}, KeyOf{}, Equal{})
          .value_or({.root = nullptr, .replaced = true});
  ASSERT_THAT(missing.root, NotNull());
  EXPECT_THAT(missing.root, Eq(root));
  EXPECT_THAT(missing.replaced, Eq(false));
  EXPECT_THAT(root->use_count(), Eq(2));
  Node::Release(source, missing.root);
}

TEST_F(HamtReplaceTest, ReplacesDeepCollisionValueWithoutChangingTheOriginalSnapshot) {
  constexpr std::uint64_t kHash = 1 + (std::uint64_t{3} << 15);
  Insert(Entry{.hash = 1, .key = 10, .value = 10});
  Insert(Entry{.hash = kHash, .key = 20, .value = 20});
  Insert(Entry{.hash = kHash, .key = 30, .value = 30});
  constexpr Entry kReplacement{.hash = kHash, .key = 20, .value = 99};
  const auto changed = TryReplaceHamtEntry(source, root, kHash, 20, kReplacement, HashOf{}, KeyOf{}, Equal{})
                           .value_or({.root = nullptr, .replaced = false});
  ASSERT_THAT(changed.root, NotNull());
  EXPECT_THAT(changed.replaced, Eq(true));
  const Entry* original = Find(root, kHash, 20);
  const Entry* updated = Find(changed.root, kHash, 20);
  const Entry* other = Find(changed.root, kHash, 30);
  ASSERT_THAT(original, NotNull());
  ASSERT_THAT(updated, NotNull());
  ASSERT_THAT(other, NotNull());
  EXPECT_THAT(original->value, Eq(20));
  EXPECT_THAT(updated->value, Eq(99));
  EXPECT_THAT(other->value, Eq(30));
  Node::Release(source, changed.root);
}

TEST_F(HamtReplaceTest, AllocationFailurePreservesDirectEntry) {
  Insert(Entry{.hash = 1, .key = 10, .value = 10});
  mbo::memory::InlineBlockSource<1> exhausted;
  constexpr Entry kReplacement{.hash = 1, .key = 10, .value = 99};
  EXPECT_THAT(
      TryReplaceHamtEntry(exhausted, root, std::uint64_t{1}, 10, kReplacement, HashOf{}, KeyOf{}, Equal{}),
      Eq(std::nullopt));
  const Entry* unchanged = Find(root, 1, 10);
  ASSERT_THAT(unchanged, NotNull());
  EXPECT_THAT(unchanged->value, Eq(10));
  EXPECT_THAT(root->use_count(), Eq(1));
}

TEST_F(HamtReplaceTest, DirectReplacementCanAliasTheExistingEntry) {
  Insert(Entry{.hash = 1, .key = 10, .value = 10});
  const Entry* const existing = Find(root, 1, 10);
  ASSERT_THAT(existing, NotNull());
  const auto changed = TryReplaceHamtEntry(source, root, std::uint64_t{1}, 10, *existing, HashOf{}, KeyOf{}, Equal{})
                           .value_or({.root = nullptr, .replaced = false});
  ASSERT_THAT(changed.root, NotNull());
  EXPECT_THAT(changed.replaced, Eq(true));
  const Entry* const copied = Find(changed.root, 1, 10);
  ASSERT_THAT(copied, NotNull());
  EXPECT_THAT(copied == existing, Eq(false));
  EXPECT_THAT(copied->value, Eq(10));
  EXPECT_THAT(existing->value, Eq(10));
  Node::Release(source, changed.root);
}

TEST_F(HamtReplaceTest, FailedChildCopyPreservesTheEntireOriginalPath) {
  Insert(Entry{.hash = 1, .key = 10, .value = 10});
  Insert(Entry{.hash = 33, .key = 20, .value = 20});
  mbo::memory::InlineBlockSource<1> exhausted;
  constexpr Entry kReplacement{.hash = 33, .key = 20, .value = 99};
  EXPECT_THAT(
      TryReplaceHamtEntry(exhausted, root, std::uint64_t{33}, 20, kReplacement, HashOf{}, KeyOf{}, Equal{}),
      Eq(std::nullopt));
  const Entry* const unchanged = Find(root, 33, 20);
  const Entry* const sibling = Find(root, 1, 10);
  ASSERT_THAT(unchanged, NotNull());
  ASSERT_THAT(sibling, NotNull());
  EXPECT_THAT(unchanged->value, Eq(20));
  EXPECT_THAT(sibling->value, Eq(10));
  EXPECT_THAT(root->use_count(), Eq(1));
  EXPECT_THAT(root->children().front()->use_count(), Eq(1));
}

TEST_F(HamtReplaceTest, FailedParentCopyReclaimsTheAlreadyCopiedChild) {
  Insert(Entry{.hash = 1, .key = 10, .value = 10});
  Insert(Entry{.hash = 33, .key = 20, .value = 20});
  mbo::memory::InlineBlockSource<4'096> single_allocation;
  constexpr Entry kReplacement{.hash = 33, .key = 20, .value = 99};
  EXPECT_THAT(
      TryReplaceHamtEntry(single_allocation, root, std::uint64_t{33}, 20, kReplacement, HashOf{}, KeyOf{}, Equal{}),
      Eq(std::nullopt));
  const Entry* const unchanged = Find(root, 33, 20);
  ASSERT_THAT(unchanged, NotNull());
  EXPECT_THAT(unchanged->value, Eq(20));
  EXPECT_THAT(root->use_count(), Eq(1));
  EXPECT_THAT(single_allocation.TryAcquire(1, 1), Optional(Field("data", &mbo::memory::MemoryBlock::data, NotNull())));
}

}  // namespace
}  // namespace mbo::container::container_internal
