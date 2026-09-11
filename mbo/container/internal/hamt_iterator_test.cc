// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_iterator.h"

#include <array>
#include <cstdint>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/internal/hamt_insert.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
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
using Iterator = HamtIterator<5, Entry>;

struct HamtIteratorTest : ::testing::Test {
  mbo::memory::NewDeleteBlockSource source;
};

TEST_F(HamtIteratorTest, TraversesEntriesInLeavesBranchesAndCollisions) {
  Node* root = nullptr;
  constexpr std::array kEntries = {
      Entry{.hash = 1, .key = 10}, Entry{.hash = 1 + (std::uint64_t{3} << 15), .key = 20}, Entry{.hash = 7, .key = 30},
      Entry{.hash = 7, .key = 40}};
  for (const Entry entry : kEntries) {
    auto inserted =
        TryInsertHamtEntry<std::uint64_t, 5>(source, root, entry.hash, entry.key, entry, HashOf{}, KeyOf{}, Equal{});
    ASSERT_THAT(inserted, Optional(_));
    Node::Release(source, root);
    root = inserted->root;
  }

  std::vector<int> keys;
  for (Iterator iter(root); iter != Iterator{}; ++iter) {
    keys.push_back(iter->key);
  }
  EXPECT_THAT(keys, ElementsAre(10, 20, 30, 40));
  Node::Release(source, root);
}

TEST_F(HamtIteratorTest, DefaultAndEmptyIteratorsAreEnd) {
  EXPECT_THAT(Iterator{}, Eq(Iterator(nullptr)));
}

TEST_F(HamtIteratorTest, IsAMultiPassForwardIterator) {
  auto inserted = TryInsertHamtEntry<std::uint64_t, 5>(
      source, static_cast<Node*>(nullptr), 1ULL, 10, Entry{.hash = 1, .key = 10}, HashOf{}, KeyOf{}, Equal{});
  ASSERT_THAT(inserted, Optional(_));
  Iterator first(inserted->root);
  Iterator copy = first;

  EXPECT_THAT(first->key, Eq(10));
  EXPECT_THAT((copy++)->key, Eq(10));
  EXPECT_THAT(first->key, Eq(10));
  EXPECT_THAT(copy, Eq(Iterator{}));
  Node::Release(source, inserted->root);
}

static_assert(std::forward_iterator<Iterator>);

}  // namespace
}  // namespace mbo::container::container_internal
