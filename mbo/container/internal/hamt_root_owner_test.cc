// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_root_owner.h"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

struct CountingSource final {
  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr bool supports_recoverable_failure = true;

  // NOLINTNEXTLINE(readability-identifier-naming): block-source contract.
  static constexpr std::size_t max_alignment() noexcept { return mbo::memory::NewDeleteBlockSource::max_alignment(); }

  std::optional<mbo::memory::MemoryBlock> TryAcquire(std::size_t size, std::size_t alignment) noexcept {
    const auto block = mbo::memory::NewDeleteBlockSource::TryAcquire(size, alignment);
    if (block) {
      ++acquired;
    }
    return block;
  }

  void Release(mbo::memory::MemoryBlock block) noexcept {
    ++released;
    mbo::memory::NewDeleteBlockSource::Release(block);
  }

  std::size_t acquired = 0;
  std::size_t released = 0;
};

using Node = HamtSharedNode<5, int>;
using Owner = HamtRootOwner<5, int, CountingSource>;

struct HamtRootOwnerTest : ::testing::Test {
  Node* Create(CountingSource& source, int value) {
    const auto entries = std::to_array<int>({value});
    return Node::TryCreateCollision(source, entries).value_or(nullptr);
  }
};

TEST_F(HamtRootOwnerTest, CopiesRetainAndOnlyTheLastOwnerReleases) {
  CountingSource source;
  auto* const root = Create(source, 10);
  ASSERT_THAT(root, NotNull());
  {
    Owner first(source, root);
    Owner second = first;
    EXPECT_THAT(root->use_count(), Eq(2));
    first.reset();
    EXPECT_THAT(first.get(), IsNull());
    ASSERT_THAT(second.get(), NotNull());
    EXPECT_THAT(second.get()->entries(), ElementsAre(10));
    EXPECT_THAT(root->use_count(), Eq(1));
    EXPECT_THAT(source.released, Eq(0));
  }
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtRootOwnerTest, MovesTransferWithoutRetainingAndLeaveReusableEmptyOwners) {
  CountingSource source;
  auto* const root = Create(source, 10);
  ASSERT_THAT(root, NotNull());
  Owner first(source, root);
  Owner second(std::move(first));
  EXPECT_THAT(first.get(), IsNull());
  EXPECT_THAT(std::addressof(first.source()), Eq(&source));
  EXPECT_THAT(root->use_count(), Eq(1));
  first = std::move(second);
  EXPECT_THAT(second.get(), IsNull());
  EXPECT_THAT(first.get(), Eq(root));
  EXPECT_THAT(root->use_count(), Eq(1));
}

TEST_F(HamtRootOwnerTest, AssignmentAndSwapKeepRootsPairedWithTheirAllocationSources) {
  CountingSource first_source;
  CountingSource second_source;
  auto* const first_root = Create(first_source, 10);
  auto* const second_root = Create(second_source, 20);
  ASSERT_THAT(first_root, NotNull());
  ASSERT_THAT(second_root, NotNull());
  Owner first(first_source, first_root);
  Owner second(second_source, second_root);
  swap(first, second);
  EXPECT_THAT(first.get(), Eq(second_root));
  EXPECT_THAT(std::addressof(first.source()), Eq(&second_source));
  first = second;
  EXPECT_THAT(second_source.released, Eq(1));
  EXPECT_THAT(first.get(), Eq(first_root));
  EXPECT_THAT(first_root->use_count(), Eq(2));
  first.reset();
  second.reset();
  EXPECT_THAT(first_source.released, Eq(1));
}

TEST_F(HamtRootOwnerTest, ReleaseTransfersTheReferenceWithoutDestroyingIt) {
  CountingSource source;
  auto* const root = Create(source, 10);
  ASSERT_THAT(root, NotNull());
  Owner owner(source, root);
  auto* const released = owner.release();
  EXPECT_THAT(released, Eq(root));
  EXPECT_THAT(owner.get(), IsNull());
  EXPECT_THAT(source.released, Eq(0));
  Node::Release(source, released);
  EXPECT_THAT(source.released, Eq(1));
}

TEST_F(HamtRootOwnerTest, MoveAssignmentReleasesTheDestinationThroughItsPreviousSource) {
  CountingSource first_source;
  CountingSource second_source;
  auto* const first_root = Create(first_source, 10);
  auto* const second_root = Create(second_source, 20);
  ASSERT_THAT(first_root, NotNull());
  ASSERT_THAT(second_root, NotNull());
  Owner first(first_source, first_root);
  Owner second(second_source, second_root);
  first = std::move(second);
  EXPECT_THAT(first_source.released, Eq(1));
  EXPECT_THAT(second.get(), IsNull());
  EXPECT_THAT(first.get(), Eq(second_root));
  EXPECT_THAT(second_root->use_count(), Eq(1));
  EXPECT_THAT(std::addressof(first.source()), Eq(&second_source));
  first.reset();
  EXPECT_THAT(second_source.released, Eq(1));
}

TEST_F(HamtRootOwnerTest, EmptyCopiesMovesAndResetsDoNotAllocateOrReleaseBlocks) {
  CountingSource source;
  Owner empty(source);
  Owner copied(empty);
  Owner moved(std::move(copied));
  empty = moved;
  moved.reset();
  EXPECT_THAT(empty.get(), IsNull());
  EXPECT_THAT(copied.get(), IsNull());
  EXPECT_THAT(moved.get(), IsNull());
  EXPECT_THAT(source.acquired, Eq(0));
  EXPECT_THAT(source.released, Eq(0));
}

TEST_F(HamtRootOwnerTest, SelfCopyAndAdoptingARetainedSameRootPreserveOneReference) {
  CountingSource source;
  auto* const root = Create(source, 10);
  ASSERT_THAT(root, NotNull());
  Owner owner(source, root);
  const Owner& same = owner;
  owner = same;
  EXPECT_THAT(owner.get(), Eq(root));
  EXPECT_THAT(root->use_count(), Eq(1));
  Node::Retain(root);
  owner.reset(root);
  EXPECT_THAT(root->use_count(), Eq(1));
  EXPECT_THAT(source.released, Eq(0));
}

static_assert(std::is_nothrow_copy_constructible_v<Owner>);
static_assert(std::is_nothrow_copy_assignable_v<Owner>);
static_assert(std::is_nothrow_move_constructible_v<Owner>);
static_assert(std::is_nothrow_move_assignable_v<Owner>);
static_assert(std::is_nothrow_destructible_v<Owner>);

}  // namespace
}  // namespace mbo::container::container_internal
