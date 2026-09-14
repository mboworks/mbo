// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_owned_tree.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/hamt_options.h"
#include "mbo/container/internal/hamt_node_value.h"
#include "mbo/container/internal/hamt_tree.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

struct Hash final {
  std::uint64_t operator()(int key) const noexcept { return static_cast<std::uint64_t>(key) ^ seed; }

  std::uint64_t seed = 0;
};

struct KeyOf final {
  int operator()(int entry) const noexcept { return entry; }
};

struct Equal final {
  bool operator()(int first, int second) const noexcept { return first == second; }
};

using Source = mbo::memory::NewDeleteBlockSource;
using Tree = HamtTree<HamtOptions{}, int, Hash, KeyOf, Equal, Source>;
using Owned = HamtOwnedTree<Tree, Source>;

struct HamtOwnedTreeTest : ::testing::Test {};

TEST_F(HamtOwnedTreeTest, PayloadAllocatedFromTheTreeDomainOutlivesTheTree) {
  using Payload = HamtNodeValue<int, Source>;
  Payload retained;
  {
    auto created = Owned::TryCreate(Hash{}, KeyOf{}, Equal{});
    if (!created) {
      FAIL() << "allocation-domain creation failed";
      return;
    }
    EXPECT_THAT(created->tree().try_insert(1).changed, Eq(true));
    auto payload = Payload::TryCreate(created->domain(), 42);
    ASSERT_THAT(payload.has_value(), Eq(true));
    retained = std::move(payload).value_or(Payload{});
  }
  ASSERT_THAT(retained.get(), NotNull());
  EXPECT_THAT(*retained.get(), Eq(42));
}

TEST_F(HamtOwnedTreeTest, RetainedDomainKeepsThePairedSourceAliveAfterTheContainerDisappears) {
  Owned::domain_type retained;
  const Source* address = nullptr;
  {
    auto created = Owned::TryCreate(Hash{}, KeyOf{}, Equal{});
    if (!created) {
      FAIL() << "allocation-domain creation failed";
      return;
    }
    retained = created->domain();
    address = created->domain().get();
    const Owned snapshot = *created;
    EXPECT_THAT(snapshot.domain().get(), Eq(address));
  }
  ASSERT_THAT(retained.get(), NotNull());
  EXPECT_THAT(retained.get(), Eq(address));
  auto block = retained.get()->TryAcquire(64, alignof(std::max_align_t));
  if (!block) {
    FAIL() << "retained source allocation failed";
    return;
  }
  retained.get()->Release(*block);
}

TEST_F(HamtOwnedTreeTest, SnapshotOutlivesOriginalAndPreservesItsValues) {
  std::optional<Owned> snapshot;
  {
    auto original = Owned::TryCreate(Hash{}, KeyOf{}, Equal{});
    if (!original) {
      FAIL() << "allocation-domain creation failed";
      return;
    }
    EXPECT_THAT(original->tree().try_insert(1).changed, Eq(true));
    snapshot = *original;
    EXPECT_THAT(original->tree().try_insert(2).changed, Eq(true));
    if (!snapshot) {
      FAIL() << "snapshot was not constructed";
      return;
    }
    EXPECT_THAT(snapshot->tree().contains(2), Eq(false));
  }
  if (!snapshot) {
    FAIL() << "snapshot was not constructed";
    return;
  }
  EXPECT_THAT(snapshot->tree().contains(1), Eq(true));
  EXPECT_THAT(snapshot->tree().try_insert(3).changed, Eq(true));
  EXPECT_THAT(snapshot->tree().size(), Eq(2));
}

TEST_F(HamtOwnedTreeTest, MoveLeavesReusableEmptyContainerAndAssignmentsKeepDomainsPaired) {
  auto created = Owned::TryCreate(Hash{}, KeyOf{}, Equal{});
  if (!created) {
    FAIL() << "allocation-domain creation failed";
    return;
  }
  Owned first = std::move(*created);
  EXPECT_THAT(first.tree().try_insert(1).changed, Eq(true));
  Owned moved = std::move(first);
  EXPECT_THAT(first.tree().empty(), Eq(true));
  EXPECT_THAT(first.tree().try_insert(2).changed, Eq(true));
  EXPECT_THAT(moved.tree().Find(1), NotNull());
  EXPECT_THAT(moved.tree().Find(2), IsNull());
  first = moved;
  EXPECT_THAT(first.tree().Find(1), NotNull());
  first = std::move(moved);
  EXPECT_THAT(moved.tree().empty(), Eq(true));
  EXPECT_THAT(moved.tree().try_insert(3).changed, Eq(true));
  swap(first, moved);
  EXPECT_THAT(first.tree().contains(3), Eq(true));
  EXPECT_THAT(moved.tree().contains(1), Eq(true));
}

TEST_F(HamtOwnedTreeTest, AssignmentAcrossIndependentDomainsKeepsBothContainersUsable) {
  auto first = Owned::TryCreate(Hash{}, KeyOf{}, Equal{});
  auto second = Owned::TryCreate(Hash{}, KeyOf{}, Equal{});
  if (!first || !second) {
    FAIL() << "allocation-domain creation failed";
    return;
  }
  EXPECT_THAT(first->tree().try_insert(1).changed, Eq(true));
  EXPECT_THAT(second->tree().try_insert(2).changed, Eq(true));
  *first = *second;
  EXPECT_THAT(first->tree().contains(1), Eq(false));
  EXPECT_THAT(first->tree().contains(2), Eq(true));
  second.reset();
  EXPECT_THAT(first->tree().try_insert(3).changed, Eq(true));
  EXPECT_THAT(first->tree().contains(2), Eq(true));
}

TEST_F(HamtOwnedTreeTest, CloneOwnsItsNewDomainAndSurvivesTheOriginal) {
  auto original = Owned::TryCreate(Hash{.seed = 29}, KeyOf{}, Equal{});
  if (!original) {
    FAIL() << "allocation-domain creation failed";
    return;
  }
  EXPECT_THAT(original->tree().try_insert(1).changed, Eq(true));
  auto cloned = original->try_clone_to<Source>();
  if (!cloned) {
    FAIL() << "clone failed";
    return;
  }
  EXPECT_THAT(cloned->tree().Find(1) == original->tree().Find(1), Eq(false));
  EXPECT_THAT(cloned->tree().hash_function().seed, Eq(29));
  original.reset();
  EXPECT_THAT(cloned->tree().contains(1), Eq(true));
  EXPECT_THAT(cloned->tree().try_insert(2).changed, Eq(true));
}

TEST_F(HamtOwnedTreeTest, ConsumingClonePreservesOriginalOnFailureAndEmptiesItOnSuccess) {
  auto original = Owned::TryCreate(Hash{}, KeyOf{}, Equal{});
  if (!original) {
    FAIL() << "allocation-domain creation failed";
    return;
  }
  EXPECT_THAT(original->tree().try_insert(1).changed, Eq(true));
  auto failed = std::move(*original).try_clone_to<mbo::memory::InlineBlockSource<1>>();
  EXPECT_THAT(failed.has_value(), Eq(false));
  EXPECT_THAT(original->tree().contains(1), Eq(true));
  auto cloned = std::move(*original).try_clone_to<Source>();
  EXPECT_THAT(cloned.has_value(), Eq(true));
  EXPECT_THAT(original->tree().empty(), Eq(true));
  EXPECT_THAT(original->tree().try_insert(3).changed, Eq(true));
}

TEST_F(HamtOwnedTreeTest, EmptyCloneNeedsNoDestinationNodeStorage) {
  auto original = Owned::TryCreate(Hash{}, KeyOf{}, Equal{});
  if (!original) {
    FAIL() << "allocation-domain creation failed";
    return;
  }
  auto cloned = original->try_clone_to<mbo::memory::InlineBlockSource<1>>();
  if (!cloned) {
    FAIL() << "empty clone failed";
    return;
  }
  EXPECT_THAT(cloned->tree().empty(), Eq(true));
  EXPECT_THAT(cloned->tree().begin() == cloned->tree().end(), Eq(true));
  EXPECT_THAT(original->tree().empty(), Eq(true));
}

TEST_F(HamtOwnedTreeTest, OwnedBoundedSourceReportsFailureWithoutChangingTheTree) {
  using BoundedSource = mbo::memory::InlineBlockSource<1>;
  using BoundedTree = HamtTree<HamtOptions{}, int, Hash, KeyOf, Equal, BoundedSource>;
  using BoundedOwned = HamtOwnedTree<BoundedTree, BoundedSource>;
  auto created = BoundedOwned::TryCreate(Hash{}, KeyOf{}, Equal{});
  if (!created) {
    FAIL() << "allocation-domain creation failed";
    return;
  }
  const auto inserted = created->tree().try_insert(1);
  EXPECT_THAT(inserted.changed, Eq(false));
  EXPECT_THAT(inserted.error, Eq(HamtError::kAllocationExhausted));
  EXPECT_THAT(created->tree().empty(), Eq(true));
}
}  // namespace
}  // namespace mbo::container::container_internal
