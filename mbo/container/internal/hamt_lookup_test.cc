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
  constexpr bool operator()(int lhs, long rhs) const noexcept { return lhs == rhs; }
};

using Node = HamtSharedNode<5, Entry>;

struct HamtLookupTest : ::testing::Test {};

TEST_F(HamtLookupTest, TraversesBitmapNodesAndTerminalCollisions) {
  mbo::memory::NewDeleteBlockSource source;
  constexpr std::array collisions = {Entry{.hash = 2, .key = 20}, Entry{.hash = 2, .key = 21}};
  const auto collision = Node::TryCreateCollision(source, collisions);
  ASSERT_THAT(collision, Optional(_));

  Node::index_type root_index;
  ASSERT_THAT(root_index.insert_data(1), Eq(true));
  ASSERT_THAT(root_index.insert_node(2), Eq(true));
  constexpr std::array root_entries = {Entry{.hash = 1, .key = 10}};
  const std::array<Node*, 1> children = {*collision};
  const auto root = Node::TryCreate(source, root_index, root_entries, children);
  ASSERT_THAT(root, Optional(_));
  Node::Release(source, *collision);

  const Entry* direct = FindHamtEntry(*root, 1ULL, 10L, HashOf{}, KeyOf{}, Equal{});
  const Entry* collided = FindHamtEntry(*root, 2ULL, 21L, HashOf{}, KeyOf{}, Equal{});
  ASSERT_THAT(direct, NotNull());
  ASSERT_THAT(collided, NotNull());
  EXPECT_THAT(direct->key, Eq(10));
  EXPECT_THAT(collided->key, Eq(21));
  EXPECT_THAT(FindHamtEntry(*root, 2ULL, 22L, HashOf{}, KeyOf{}, Equal{}), Eq(nullptr));
  EXPECT_THAT(FindHamtEntry(*root, 3ULL, 10L, HashOf{}, KeyOf{}, Equal{}), Eq(nullptr));

  Node::Release(source, *root);
}

}  // namespace
}  // namespace mbo::container::container_internal
