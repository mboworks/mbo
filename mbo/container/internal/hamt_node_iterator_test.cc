// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_node_iterator.h"

#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/internal/hamt_iterator.h"
#include "mbo/container/internal/hamt_shared_node.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::Eq;
using ::testing::NotNull;

struct BorrowedHandle final {
  const int* value;

  // NOLINTNEXTLINE(readability-identifier-naming): handle vocabulary.
  const int* get() const noexcept { return value; }
};

using Iterator = HamtNodeIterator<const BorrowedHandle*>;
static_assert(std::forward_iterator<Iterator>);
static_assert(std::is_same_v<Iterator::reference, const int&>);

struct HamtNodeIteratorTest : ::testing::Test {};

TEST_F(HamtNodeIteratorTest, ActualHamtTraversalPreservesRangeIdentityAndReachesEnd) {
  using Node = HamtSharedNode<5, BorrowedHandle>;
  using Traversal = HamtIterator<5, BorrowedHandle>;
  using Projected = HamtNodeIterator<Traversal>;
  static_assert(std::forward_iterator<Projected>);
  const int first = 42;
  const int second = 99;
  const auto handles =
      std::to_array<BorrowedHandle>({BorrowedHandle{.value = &first}, BorrowedHandle{.value = &second}});
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  ASSERT_THAT(index.InsertData(2), Eq(true));
  mbo::memory::NewDeleteBlockSource source;
  auto* const root = Node::TryCreate(source, index, handles, {}).value_or(nullptr);
  ASSERT_THAT(root, NotNull());
  const int other_range = 0;
  Projected position{Traversal(root)};
  const Projected other{Traversal(root, &other_range)};
  EXPECT_THAT(position == other, Eq(false));
  EXPECT_THAT(*position, Eq(42));
  ++position;
  ASSERT_THAT(position == Projected{}, Eq(false));
  EXPECT_THAT(*position, Eq(99));
  ++position;
  EXPECT_THAT(position == Projected{}, Eq(true));
  Node::Release(source, root);
}

TEST_F(HamtNodeIteratorTest, ExposesValuesAndPreservesIndependentTraversalCopies) {
  const int first = 42;
  const int second = 99;
  const auto handles =
      std::to_array<BorrowedHandle>({BorrowedHandle{.value = &first}, BorrowedHandle{.value = &second}});
  Iterator position(handles.data());
  Iterator copied = position;
  EXPECT_THAT(*position, Eq(42));
  EXPECT_THAT(position.operator->(), Eq(&first));
  EXPECT_THAT(std::addressof(*position), Eq(&first));
  const Iterator previous = position++;
  EXPECT_THAT(previous == copied, Eq(true));
  EXPECT_THAT(position == copied, Eq(false));
  EXPECT_THAT(*position, Eq(99));
  ++copied;
  EXPECT_THAT(position == copied, Eq(true));
  EXPECT_THAT(position.operator->(), Eq(&second));
}

TEST_F(HamtNodeIteratorTest, ValueInitializedIteratorsCompareEqual) {
  const Iterator first;
  const Iterator second;
  EXPECT_THAT(first == second, Eq(true));
}

TEST_F(HamtNodeIteratorTest, CollisionBucketsProjectValuesForEverySupportedFragmentWidth) {
  const int first = 42;
  const int second = 99;
  const auto handles =
      std::to_array<BorrowedHandle>({BorrowedHandle{.value = &first}, BorrowedHandle{.value = &second}});
  const auto check = [&]<std::size_t FragmentBits>() {
    using Node = HamtSharedNode<FragmentBits, BorrowedHandle>;
    using Projected = HamtNodeIterator<HamtIterator<FragmentBits, BorrowedHandle>>;
    mbo::memory::NewDeleteBlockSource source;
    auto* const root = Node::TryCreateCollision(source, handles).value_or(nullptr);
    ASSERT_THAT(root, NotNull());
    Projected position{HamtIterator<FragmentBits, BorrowedHandle>(root)};
    EXPECT_THAT(*position, Eq(42));
    ++position;
    ASSERT_THAT(position == Projected{}, Eq(false));
    EXPECT_THAT(*position, Eq(99));
    ++position;
    EXPECT_THAT(position == Projected{}, Eq(true));
    Node::Release(source, root);
  };
  check.operator()<4>();
  check.operator()<5>();
  check.operator()<6>();
  check.operator()<7>();
}

}  // namespace
}  // namespace mbo::container::container_internal
