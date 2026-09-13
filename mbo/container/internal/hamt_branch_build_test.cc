// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_branch_build.h"

#include <cstddef>
#include <cstdint>
#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/internal/hamt_lookup.h"
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
  constexpr bool operator()(int lhs, int rhs) const noexcept { return lhs == rhs; }
};

using Node = HamtSharedNode<5, Entry>;

struct HamtBranchBuildTest : ::testing::Test {};

TEST_F(HamtBranchBuildTest, BuildsEveryCommonPrefixLevelAndDivergence) {
  mbo::memory::NewDeleteBlockSource source;
  constexpr std::uint64_t kFirstHash = 3;
  constexpr std::uint64_t kSecondHash = 3 + (std::uint64_t{7} << 15);
  const auto root = TryBuildHamtBranch<5>(
      source, kFirstHash, Entry{.hash = kFirstHash, .key = 10}, kSecondHash, Entry{.hash = kSecondHash, .key = 20}, 0);
  ASSERT_THAT(root, Optional(_));

  const Entry* first = FindHamtEntry(*root, kFirstHash, 10, HashOf{}, KeyOf{}, Equal{});
  const Entry* second = FindHamtEntry(*root, kSecondHash, 20, HashOf{}, KeyOf{}, Equal{});
  ASSERT_THAT(first, NotNull());
  ASSERT_THAT(second, NotNull());
  EXPECT_THAT(first->key, Eq(10));
  EXPECT_THAT(second->key, Eq(20));
  Node::Release(source, *root);
}

TEST_F(HamtBranchBuildTest, BuildsTerminalFullHashCollisionNodes) {
  mbo::memory::NewDeleteBlockSource source;
  const auto root = TryBuildHamtBranch<5>(
      source, std::uint64_t{7}, Entry{.hash = 7, .key = 10}, std::uint64_t{7}, Entry{.hash = 7, .key = 20}, 0);
  ASSERT_THAT(root, Optional(_));
  EXPECT_THAT((*root)->is_collision(), Eq(true));
  ASSERT_THAT(FindHamtEntry(*root, std::uint64_t{7}, 20, HashOf{}, KeyOf{}, Equal{}), NotNull());
  Node::Release(source, *root);
}

struct BudgetSource final {
  static constexpr bool supports_recoverable_failure = true;

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
        TryBuildHamtBranch<5>(source, kFirstHash, Entry{kFirstHash, 10}, kSecondHash, Entry{kSecondHash, 20}, 0),
        Eq(std::nullopt));
    EXPECT_THAT(source.acquired, Eq(allowed));
    EXPECT_THAT(source.released, Eq(allowed));
  }
}

TEST_F(HamtBranchBuildTest, OrdersDivergingEntriesByTheirFragmentsNotInsertionOrder) {
  mbo::memory::NewDeleteBlockSource source;
  const auto root = TryBuildHamtBranch<5>(source, std::uint64_t{2}, Entry{2, 10}, std::uint64_t{1}, Entry{1, 20}, 0);
  ASSERT_THAT(root, Optional(_));
  ASSERT_THAT((*root)->entries().size(), Eq(2));
  EXPECT_THAT((*root)->entries()[0].key, Eq(20));
  EXPECT_THAT((*root)->entries()[1].key, Eq(10));
  Node::Release(source, *root);
}

TEST_F(HamtBranchBuildTest, RejectsStartingPastTheHashBeforeAllocation) {
  BudgetSource source{.remaining = 4};
  constexpr std::size_t kEnd = HamtHashPath<std::uint64_t, 5>::kLevels;
  EXPECT_THAT(
      TryBuildHamtBranch<5>(source, std::uint64_t{1}, Entry{1, 10}, std::uint64_t{1}, Entry{1, 20}, kEnd + 1),
      Eq(std::nullopt));
  EXPECT_THAT(
      TryBuildHamtBranch<5>(source, std::uint64_t{1}, Entry{1, 10}, std::uint64_t{2}, Entry{2, 20}, kEnd),
      Eq(std::nullopt));
  EXPECT_THAT(source.acquired, Eq(0));
}

}  // namespace
}  // namespace mbo::container::container_internal
