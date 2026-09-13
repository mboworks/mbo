// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_lookup.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::_;
using ::testing::Eq;
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
  constexpr bool operator()(int lhs, std::int64_t rhs) const noexcept { return lhs == rhs; }
};

using Node = HamtSharedNode<5, Entry>;

struct HamtLookupTest : ::testing::Test {};

struct NonCopyingKeyOf final {
  explicit NonCopyingKeyOf(int& calls) noexcept : calls(calls) {}

  NonCopyingKeyOf(const NonCopyingKeyOf&) = delete;
  NonCopyingKeyOf& operator=(const NonCopyingKeyOf&) = delete;
  NonCopyingKeyOf(NonCopyingKeyOf&&) = delete;
  NonCopyingKeyOf& operator=(NonCopyingKeyOf&&) = delete;
  ~NonCopyingKeyOf() = default;

  int operator()(const Entry& entry) const noexcept {
    ++calls;
    return entry.key;
  }

  int& calls;
};

TEST_F(HamtLookupTest, BorrowsCallableStateAndRejectsHashesBeforeKeyExtraction) {
  mbo::memory::NewDeleteBlockSource source;
  constexpr auto kEntries = std::to_array<Entry>({Entry{2, 20}, Entry{2, 21}});
  const auto node = Node::TryCreateCollision(source, kEntries);
  ASSERT_THAT(node, Optional(_));
  int calls = 0;
  const NonCopyingKeyOf key_of(calls);
  EXPECT_THAT(FindHamtEntry(*node, std::uint64_t{34}, 21, HashOf{}, key_of, Equal{}), Eq(nullptr));
  EXPECT_THAT(calls, Eq(0));
  const Entry* found = FindHamtEntry(*node, std::uint64_t{2}, 21, HashOf{}, key_of, Equal{});
  ASSERT_THAT(found, NotNull());
  EXPECT_THAT(found->key, Eq(21));
  EXPECT_THAT(calls, Eq(2));
  Node::Release(source, *node);
}

TEST_F(HamtLookupTest, TraversesBitmapNodesAndTerminalCollisions) {
  mbo::memory::NewDeleteBlockSource source;
  constexpr auto kCollisions = std::to_array<Entry>({Entry{.hash = 2, .key = 20}, Entry{.hash = 2, .key = 21}});
  const auto collision = Node::TryCreateCollision(source, kCollisions);
  ASSERT_THAT(collision, Optional(_));

  Node::index_type root_index;
  ASSERT_THAT(root_index.InsertData(1), Eq(true));
  ASSERT_THAT(root_index.InsertNode(2), Eq(true));
  constexpr auto kRootEntries = std::to_array<Entry>({Entry{.hash = 1, .key = 10}});
  const auto children = std::to_array<Node*>({*collision});
  const auto root = Node::TryCreate(source, root_index, kRootEntries, children);
  ASSERT_THAT(root, Optional(_));
  Node::Release(source, *collision);

  const Entry* direct = FindHamtEntry(*root, std::uint64_t{1}, std::int64_t{10}, HashOf{}, KeyOf{}, Equal{});
  const Entry* collided = FindHamtEntry(*root, std::uint64_t{2}, std::int64_t{21}, HashOf{}, KeyOf{}, Equal{});
  ASSERT_THAT(direct, NotNull());
  ASSERT_THAT(collided, NotNull());
  EXPECT_THAT(direct->key, Eq(10));
  EXPECT_THAT(collided->key, Eq(21));
  EXPECT_THAT(FindHamtEntry(*root, std::uint64_t{2}, std::int64_t{22}, HashOf{}, KeyOf{}, Equal{}), Eq(nullptr));
  EXPECT_THAT(FindHamtEntry(*root, std::uint64_t{3}, std::int64_t{10}, HashOf{}, KeyOf{}, Equal{}), Eq(nullptr));

  Node::Release(source, *root);
}

TEST_F(HamtLookupTest, NullRootIsAnEmptyLookup) {
  EXPECT_THAT(
      FindHamtEntry(static_cast<const Node*>(nullptr), std::uint64_t{0}, 0, HashOf{}, KeyOf{}, Equal{}), Eq(nullptr));
}

TEST_F(HamtLookupTest, TraversesTheEntireHashWidthForEverySupportedFragmentSize) {
  const auto check_width = []<std::size_t FragmentBits>() {
    using DeepNode = HamtSharedNode<FragmentBits, Entry>;
    using Path = HamtHashPath<std::uint64_t, FragmentBits>;
    mbo::memory::NewDeleteBlockSource source;
    constexpr std::uint64_t kHash = std::uint64_t{1} << 63;
    constexpr Path kPath(kHash);
    typename DeepNode::index_type leaf_index;
    ASSERT_THAT(leaf_index.InsertData(kPath.Fragment(Path::kLevels - 1)), Eq(true));
    constexpr auto kEntries = std::to_array<Entry>({Entry{.hash = kHash, .key = 42}});
    const auto leaf = DeepNode::TryCreate(source, leaf_index, kEntries, {});
    ASSERT_THAT(leaf, Optional(_));
    DeepNode* root = *leaf;
    for (std::size_t level = Path::kLevels - 1; level > 0; --level) {
      typename DeepNode::index_type index;
      ASSERT_THAT(index.InsertNode(kPath.Fragment(level - 1)), Eq(true));
      const auto children = std::to_array<DeepNode*>({root});
      const auto parent = DeepNode::TryCreate(source, index, {}, children);
      ASSERT_THAT(parent, Optional(_));
      DeepNode::Release(source, root);
      root = *parent;
    }
    EXPECT_THAT(FindHamtEntry(root, kHash, 42, HashOf{}, KeyOf{}, Equal{}), Eq(&(*leaf)->entries().front()));
    EXPECT_THAT(FindHamtEntry(root, kHash, 43, HashOf{}, KeyOf{}, Equal{}), Eq(nullptr));
    EXPECT_THAT(FindHamtEntry(root, std::uint64_t{0}, 42, HashOf{}, KeyOf{}, Equal{}), Eq(nullptr));
    DeepNode::Release(source, root);
  };
  check_width.operator()<4>();
  check_width.operator()<5>();
  check_width.operator()<6>();
  check_width.operator()<7>();
}

TEST_F(HamtLookupTest, DirectEntryRequiresBothFullHashAndKeyToMatch) {
  mbo::memory::NewDeleteBlockSource source;
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  constexpr auto kEntries = std::to_array<Entry>({Entry{.hash = 1, .key = 10}});
  const auto node = Node::TryCreate(source, index, kEntries, {});
  ASSERT_THAT(node, Optional(_));
  EXPECT_THAT(FindHamtEntry(*node, std::uint64_t{1}, 11, HashOf{}, KeyOf{}, Equal{}), Eq(nullptr));
  EXPECT_THAT(FindHamtEntry(*node, std::uint64_t{33}, 10, HashOf{}, KeyOf{}, Equal{}), Eq(nullptr));
  Node::Release(source, *node);
}

}  // namespace
}  // namespace mbo::container::container_internal
