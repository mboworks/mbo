// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_branch_build.h"

#include <cstdint>

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
  const auto root =
      TryBuildHamtBranch<5>(source, 7ULL, Entry{.hash = 7, .key = 10}, 7ULL, Entry{.hash = 7, .key = 20}, 0);
  ASSERT_THAT(root, Optional(_));
  EXPECT_THAT((*root)->is_collision(), Eq(true));
  ASSERT_THAT(FindHamtEntry(*root, 7ULL, 20, HashOf{}, KeyOf{}, Equal{}), NotNull());
  Node::Release(source, *root);
}

}  // namespace
}  // namespace mbo::container::container_internal
