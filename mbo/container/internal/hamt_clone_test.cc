// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_clone.h"

#include <array>
#include <cstddef>
#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::NotNull;
using ::testing::Optional;

struct BudgetSource final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    if (remaining == 0) {
      return std::nullopt;
    }
    const auto block = mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
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

using Node = HamtSharedNode<5, int>;

struct HamtCloneTest : ::testing::Test {
  mbo::memory::NewDeleteBlockSource source;
};

struct ThrowingCopy final {
  ThrowingCopy() = default;

  ThrowingCopy(const ThrowingCopy&) noexcept(false) {}

  ThrowingCopy& operator=(const ThrowingCopy&) = default;
  ThrowingCopy(ThrowingCopy&&) noexcept = default;
  ThrowingCopy& operator=(ThrowingCopy&&) noexcept = default;
  ~ThrowingCopy() = default;
};

template<typename Entry>
concept CloneableEntry = requires(mbo::memory::NewDeleteBlockSource& source, const HamtSharedNode<5, Entry>* root) {
  TryCloneHamtTree(source, root);
};

static_assert(CloneableEntry<int>);
static_assert(!CloneableEntry<ThrowingCopy>);

struct TrackedEntry final {
  TrackedEntry(std::size_t& live, int entry_value) noexcept : value(entry_value), live_count_(&live) { ++live; }

  TrackedEntry(const TrackedEntry& other) noexcept : value(other.value), live_count_(other.live_count_) {
    ++*live_count_;
  }

  TrackedEntry& operator=(const TrackedEntry&) = delete;
  TrackedEntry(TrackedEntry&&) = delete;
  TrackedEntry& operator=(TrackedEntry&&) = delete;

  ~TrackedEntry() { --*live_count_; }

  int value;

 private:
  std::size_t* live_count_;
};

static_assert(CloneableEntry<TrackedEntry>);

TEST_F(HamtCloneTest, NontrivialCopiesAreDestroyedOnFailureAndOwnTheirSuccessfulLifetime) {
  using TrackedNode = HamtSharedNode<5, TrackedEntry>;
  std::size_t live = 0;
  {
    const std::array<TrackedEntry, 2> entries{TrackedEntry(live, 10), TrackedEntry(live, 20)};
    const std::array<TrackedEntry, 1> root_entries{TrackedEntry(live, 30)};
    auto* const collision = TrackedNode::TryCreateCollision(source, entries).value_or(nullptr);
    ASSERT_THAT(collision, NotNull());
    TrackedNode::index_type index;
    ASSERT_THAT(index.InsertNode(1), Eq(true));
    ASSERT_THAT(index.InsertNode(2), Eq(true));
    ASSERT_THAT(index.InsertData(3), Eq(true));
    const auto children = std::to_array<TrackedNode*>({collision, collision});
    auto* const root = TrackedNode::TryCreate(source, index, root_entries, children).value_or(nullptr);
    ASSERT_THAT(root, NotNull());
    TrackedNode::Release(source, collision);
    const auto original_live = live;
    EXPECT_THAT(original_live, Eq(6));
    for (std::size_t allowance = 0; allowance < 3; ++allowance) {
      BudgetSource destination{.remaining = allowance};
      EXPECT_THAT(TryCloneHamtTree(destination, root), Eq(std::nullopt));
      EXPECT_THAT(destination.acquired, Eq(destination.released));
      EXPECT_THAT(live, Eq(original_live));
    }
    BudgetSource destination{.remaining = 3};
    auto* const cloned = TryCloneHamtTree(destination, root).value_or(nullptr);
    ASSERT_THAT(cloned, NotNull());
    EXPECT_THAT(live, Eq(original_live + 5));
    EXPECT_THAT(cloned->children().front(), Ne(cloned->children().back()));
    TrackedNode::Release(source, root);
    EXPECT_THAT(live, Eq(8));
    EXPECT_THAT(cloned->entries().front().value, Eq(30));
    for (const TrackedNode* child : cloned->children()) {
      EXPECT_THAT(child->entries().front().value, Eq(10));
      EXPECT_THAT(child->entries().back().value, Eq(20));
    }
    TrackedNode::Release(destination, cloned);
    EXPECT_THAT(destination.acquired, Eq(destination.released));
    EXPECT_THAT(live, Eq(3));
  }
  EXPECT_THAT(live, Eq(0));
}

TEST_F(HamtCloneTest, NullRootSucceedsWithoutAllocating) {
  BudgetSource destination;
  EXPECT_THAT(TryCloneHamtTree(destination, static_cast<const Node*>(nullptr)), Optional(Eq(nullptr)));
  EXPECT_THAT(destination.acquired, Eq(0));
}

TEST_F(HamtCloneTest, EmptyAllocatedRootIsCopiedWithoutRetainingTheOriginal) {
  auto* const original = Node::TryCreate(source, {}, {}, {}).value_or(nullptr);
  ASSERT_THAT(original, NotNull());
  BudgetSource destination{.remaining = 1};
  auto* const cloned = TryCloneHamtTree(destination, original).value_or(nullptr);
  ASSERT_THAT(cloned, NotNull());
  EXPECT_THAT(cloned == original, Eq(false));
  EXPECT_THAT(original->use_count(), Eq(1));
  EXPECT_THAT(cloned->is_collision(), Eq(false));
  EXPECT_THAT(cloned->entries(), ElementsAre());
  EXPECT_THAT(cloned->children(), ElementsAre());
  Node::Release(source, original);
  Node::Release(destination, cloned);
  EXPECT_THAT(destination.acquired, Eq(1));
  EXPECT_THAT(destination.released, Eq(1));
}

TEST_F(HamtCloneTest, DeepCloneOwnsIndependentNodesAndRollsBackEveryFailure) {
  constexpr auto kEntries = std::to_array<int>({10, 20});
  auto* const collision = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
  ASSERT_THAT(collision, NotNull());
  Node::index_type index;
  ASSERT_THAT(index.InsertNode(1), Eq(true));
  ASSERT_THAT(index.InsertNode(2), Eq(true));
  ASSERT_THAT(index.InsertData(3), Eq(true));
  constexpr auto kRootEntries = std::to_array<int>({30});
  const auto children = std::to_array<Node*>({collision, collision});
  auto* const root = Node::TryCreate(source, index, kRootEntries, children).value_or(nullptr);
  ASSERT_THAT(root, NotNull());
  Node::Release(source, collision);
  for (std::size_t allowance = 0; allowance < 3; ++allowance) {
    BudgetSource destination{.remaining = allowance};
    EXPECT_THAT(TryCloneHamtTree(destination, root), Eq(std::nullopt));
    EXPECT_THAT(destination.acquired, Eq(allowance));
    EXPECT_THAT(destination.released, Eq(allowance));
    EXPECT_THAT(root->use_count(), Eq(1));
    EXPECT_THAT(collision->use_count(), Eq(2));
  }
  BudgetSource destination{.remaining = 3};
  auto* const cloned = TryCloneHamtTree(destination, root).value_or(nullptr);
  ASSERT_THAT(cloned, NotNull());
  EXPECT_THAT(cloned == root, Eq(false));
  EXPECT_THAT(cloned->entries(), ElementsAre(30));
  for (const Node* child : cloned->children()) {
    EXPECT_THAT(child == collision, Eq(false));
    EXPECT_THAT(child->is_collision(), Eq(true));
    EXPECT_THAT(child->entries(), ElementsAre(10, 20));
  }
  Node::Release(source, root);
  EXPECT_THAT(cloned->children().front()->entries(), ElementsAre(10, 20));
  Node::Release(destination, cloned);
  EXPECT_THAT(destination.acquired, Eq(destination.released));
}

}  // namespace
}  // namespace mbo::container::container_internal
