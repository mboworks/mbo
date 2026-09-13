// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_iterator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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
using ::testing::UnorderedElementsAre;

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

  template<std::size_t FragmentBits>
  void CheckDeepestTree() {
    using DeepNode = HamtSharedNode<FragmentBits, Entry>;
    using DeepIterator = HamtIterator<FragmentBits, Entry>;
    const auto release = [this](DeepNode* node) noexcept { DeepNode::Release(source, node); };
    std::unique_ptr<DeepNode, decltype(release)> root(nullptr, release);
    constexpr auto kEntries = std::to_array<Entry>({Entry{0, 10}, Entry{std::uint64_t{1} << 63, 20}, Entry{0, 30}});
    for (const Entry& entry : kEntries) {
      const auto inserted = TryInsertHamtEntry<std::uint64_t, FragmentBits>(
          source, root.get(), entry.hash, entry.key, entry, HashOf{}, KeyOf{}, Equal{});
      ASSERT_THAT(inserted, Optional(_));
      root.reset(inserted->root);
    }
    std::vector<int> keys;
    for (DeepIterator iter(root.get()); iter != DeepIterator{}; ++iter) {
      keys.push_back(iter->key);
    }
    EXPECT_THAT(keys, UnorderedElementsAre(10, 20, 30));
  }
};

TEST_F(HamtIteratorTest, TraversesDeepestPathsForEverySupportedFragmentWidth) {
  CheckDeepestTree<4>();
  CheckDeepestTree<5>();
  CheckDeepestTree<6>();
  CheckDeepestTree<7>();
}

TEST_F(HamtIteratorTest, TraversesEntriesInLeavesBranchesAndCollisions) {
  Node* root = nullptr;
  constexpr auto kEntries = std::to_array<Entry>(
      {Entry{.hash = 1, .key = 10}, Entry{.hash = 1 + (std::uint64_t{3} << 15), .key = 20}, Entry{.hash = 7, .key = 30},
       Entry{.hash = 7, .key = 40}});
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

TEST_F(HamtIteratorTest, ValueInitializedIteratorsEqualTheEmptyRangeEnd) {
  EXPECT_THAT(Iterator{}, Eq(Iterator(nullptr)));
}

TEST_F(HamtIteratorTest, SharedEntriesInDifferentRootRangesDoNotCompareEqual) {
  constexpr auto kEntries = std::to_array<Entry>({Entry{7, 10}, Entry{7, 20}});
  const auto child = Node::TryCreateCollision(source, kEntries);
  ASSERT_THAT(child, Optional(_));
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(7), Eq(true));
  const auto children = std::to_array<Node*>({*child});
  const auto first_root = Node::TryCreate(source, index, {}, children);
  const auto second_root = Node::TryCreate(source, index, {}, children);
  ASSERT_THAT(first_root, Optional(_));
  ASSERT_THAT(second_root, Optional(_));
  Iterator first(*first_root);
  Iterator second(*second_root);
  EXPECT_THAT(first.operator->(), Eq(second.operator->()));
  EXPECT_THAT(first == second, Eq(false));
  Node::Release(source, *child);
  Node::Release(source, *first_root);
  Node::Release(source, *second_root);
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
