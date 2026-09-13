// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_lookup.h"

#include <array>
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
