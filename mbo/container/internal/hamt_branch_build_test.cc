// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_branch_build.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/internal/hamt_lookup.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::NotNull;
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

struct HamtBranchBuildTest : ::testing::Test {};

TEST_F(HamtBranchBuildTest, BuildsEveryCommonPrefixLevelAndDivergence) {
  mbo::memory::NewDeleteBlockSource source;
  constexpr std::uint64_t kFirstHash = 3;
  constexpr std::uint64_t kSecondHash = 3 + (std::uint64_t{7} << 15);
  auto* const root = TryBuildHamtBranch<5>(
                         source, kFirstHash, Entry{.hash = kFirstHash, .key = 10}, kSecondHash,
                         Entry{.hash = kSecondHash, .key = 20}, 0)
                         .value_or(nullptr);
  ASSERT_THAT(root, NotNull());

  const Entry* first = FindHamtEntry(root, kFirstHash, 10, HashOf{}, KeyOf{}, Equal{});
  const Entry* second = FindHamtEntry(root, kSecondHash, 20, HashOf{}, KeyOf{}, Equal{});
  ASSERT_THAT(first, NotNull());
  ASSERT_THAT(second, NotNull());
  EXPECT_THAT(first->key, Eq(10));
  EXPECT_THAT(second->key, Eq(20));
  Node::Release(source, root);
}

TEST_F(HamtBranchBuildTest, BuildsTerminalFullHashCollisionNodes) {
  mbo::memory::NewDeleteBlockSource source;
  auto* const root =
      TryBuildHamtBranch<5>(
          source, std::uint64_t{7}, Entry{.hash = 7, .key = 10}, std::uint64_t{7}, Entry{.hash = 7, .key = 20}, 0)
          .value_or(nullptr);
  ASSERT_THAT(root, NotNull());
  EXPECT_THAT(root->is_collision(), Eq(true));
  ASSERT_THAT(FindHamtEntry(root, std::uint64_t{7}, 20, HashOf{}, KeyOf{}, Equal{}), NotNull());
  Node::Release(source, root);
}

struct BudgetSource final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    if (remaining == 0) {
      return std::nullopt;
    }
    auto block = mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
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

TEST_F(HamtBranchBuildTest, ReclaimsEveryPartialBranchWhenAnyAllocationFails) {
  constexpr std::uint64_t kFirstHash = 3;
  constexpr std::uint64_t kSecondHash = 3 + (std::uint64_t{7} << 15);
  for (std::size_t allowed = 0; allowed < 4; ++allowed) {
    SCOPED_TRACE(allowed);
    BudgetSource source{.remaining = allowed};
    EXPECT_THAT(
        TryBuildHamtBranch<5>(
            source, kFirstHash, Entry{.hash = kFirstHash, .key = 10}, kSecondHash,
            Entry{.hash = kSecondHash, .key = 20}, 0),
        Eq(std::nullopt));
    EXPECT_THAT(source.acquired, Eq(allowed));
    EXPECT_THAT(source.released, Eq(allowed));
  }
}

TEST_F(HamtBranchBuildTest, OrdersDivergingEntriesByTheirFragmentsNotInsertionOrder) {
  mbo::memory::NewDeleteBlockSource source;
  auto* const root =
      TryBuildHamtBranch<5>(
          source, std::uint64_t{2}, Entry{.hash = 2, .key = 10}, std::uint64_t{1}, Entry{.hash = 1, .key = 20}, 0)
          .value_or(nullptr);
  ASSERT_THAT(root, NotNull());
  EXPECT_THAT(root->entries(), ElementsAre(Field("key", &Entry::key, Eq(20)), Field("key", &Entry::key, Eq(10))));
  Node::Release(source, root);
}

TEST_F(HamtBranchBuildTest, RejectsStartingPastTheHashBeforeAllocation) {
  BudgetSource source{.remaining = 4};
  constexpr std::size_t kEnd = HamtHashPath<std::uint64_t, 5>::kLevels;
  EXPECT_THAT(
      TryBuildHamtBranch<5>(
          source, std::uint64_t{1}, Entry{.hash = 1, .key = 10}, std::uint64_t{1}, Entry{.hash = 1, .key = 20},
          kEnd + 1),
      Eq(std::nullopt));
  EXPECT_THAT(
      TryBuildHamtBranch<5>(
          source, std::uint64_t{1}, Entry{.hash = 1, .key = 10}, std::uint64_t{2}, Entry{.hash = 2, .key = 20}, kEnd),
      Eq(std::nullopt));
  EXPECT_THAT(source.acquired, Eq(0));
}

TEST_F(HamtBranchBuildTest, FailedCollisionSplitReclaimsOnlyTheNewBranchNodes) {
  constexpr auto kEntries = std::to_array<Entry>({Entry{.hash = 7, .key = 10}, Entry{.hash = 7, .key = 20}});
  constexpr std::uint64_t kNewHash = 7 + (std::uint64_t{7} << 15);
  for (std::size_t allowed = 0; allowed < 4; ++allowed) {
    SCOPED_TRACE(allowed);
    BudgetSource source{.remaining = 1};
    auto* const original = Node::TryCreateCollision(source, kEntries).value_or(nullptr);
    ASSERT_THAT(original, NotNull());
    source.remaining = allowed;
    EXPECT_THAT(
        TryBuildHamtCollisionBranch<5>(
            source, original, std::uint64_t{7}, kNewHash, Entry{.hash = kNewHash, .key = 30}, 0),
        Eq(std::nullopt));
    EXPECT_THAT(source.acquired, Eq(allowed + 1));
    EXPECT_THAT(source.released, Eq(allowed));
    EXPECT_THAT(original->use_count(), Eq(1));
    EXPECT_THAT(original->entries(), SizeIs(2));
    Node::Release(source, original);
    EXPECT_THAT(source.released, Eq(source.acquired));
  }
}

TEST_F(HamtBranchBuildTest, BuildsFullHashCollisionAtTheExhaustedPathBoundary) {
  BudgetSource source{.remaining = 1};
  constexpr std::size_t kEnd = HamtHashPath<std::uint64_t, 5>::kLevels;
  auto* const root =
      TryBuildHamtBranch<5>(
          source, std::uint64_t{1}, Entry{.hash = 1, .key = 10}, std::uint64_t{1}, Entry{.hash = 1, .key = 20}, kEnd)
          .value_or(nullptr);
  ASSERT_THAT(root, NotNull());
  EXPECT_THAT(root->is_collision(), Eq(true));
  EXPECT_THAT(root->entries(), ElementsAre(Field("key", &Entry::key, Eq(10)), Field("key", &Entry::key, Eq(20))));
  EXPECT_THAT(source.acquired, Eq(1));
  Node::Release(source, root);
  EXPECT_THAT(source.released, Eq(1));
}

}  // namespace
}  // namespace mbo::container::container_internal
