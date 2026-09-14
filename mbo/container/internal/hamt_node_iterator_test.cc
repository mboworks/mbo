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

struct EditableHandle final {
  int* value;

  // NOLINTBEGIN(readability-identifier-naming): handle vocabulary.
  const int* get() const noexcept { return value; }

  int* get_unique_mutable() noexcept { return value; }

  // NOLINTEND(readability-identifier-naming)
};

TEST_F(HamtNodeIteratorTest, PreparedMutableTraversalEditsValuesWithoutChangingMultipassPosition) {
  using EditableIterator = HamtNodeIterator<EditableHandle*, true>;
  using ReadOnlyIterator = HamtNodeIterator<const EditableHandle*>;
  static_assert(std::forward_iterator<EditableIterator>);
  static_assert(std::convertible_to<EditableIterator, ReadOnlyIterator>);
  static_assert(!std::convertible_to<ReadOnlyIterator, EditableIterator>);
  static_assert(std::is_same_v<EditableIterator::reference, int&>);
  static_assert(std::is_same_v<HamtNodeIterator<EditableHandle*>::reference, const int&>);
  int first = 42;
  int second = 99;
  auto handles = std::to_array<EditableHandle>({EditableHandle{.value = &first}, EditableHandle{.value = &second}});
  EditableIterator position(handles.data());
  const ReadOnlyIterator read_only = position;
  EXPECT_THAT(position == read_only, Eq(true));
  EXPECT_THAT(read_only == position, Eq(true));
  auto copied = position;
  *position = 10;
  EXPECT_THAT(*copied, Eq(10));
  EXPECT_THAT(position.operator->(), Eq(&first));
  ++position;
  EXPECT_THAT(position == read_only, Eq(false));
  EXPECT_THAT(position == copied, Eq(false));
  *position = 20;
  EXPECT_THAT(first, Eq(10));
  EXPECT_THAT(second, Eq(20));
  ++copied;
  EXPECT_THAT(position == copied, Eq(true));
}

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

TEST_F(HamtNodeIteratorTest, MutableHamtTraversalConvertsWithoutLosingRangeIdentity) {
  using Node = HamtSharedNode<5, EditableHandle>;
  using Mutable = HamtNodeIterator<HamtIterator<5, EditableHandle, true>, true>;
  using ReadOnly = HamtNodeIterator<HamtIterator<5, EditableHandle>>;
  static_assert(std::forward_iterator<Mutable>);
  static_assert(std::convertible_to<Mutable, ReadOnly>);
  static_assert(!std::convertible_to<ReadOnly, Mutable>);
  int value = 42;
  const auto handles = std::to_array<EditableHandle>({EditableHandle{.value = &value}});
  Node::index_type index;
  ASSERT_THAT(index.InsertData(1), Eq(true));
  mbo::memory::NewDeleteBlockSource source;
  auto* const root = Node::TryCreate(source, index, handles, {}).value_or(nullptr);
  ASSERT_THAT(root, NotNull());
  Mutable position{HamtIterator<5, EditableHandle, true>(root)};
  ReadOnly read_only = position;
  EXPECT_THAT(position == read_only, Eq(true));
  EXPECT_THAT(read_only == position, Eq(true));
  *position = 99;
  EXPECT_THAT(*read_only, Eq(99));
  ++position;
  EXPECT_THAT(position == read_only, Eq(false));
  ++read_only;
  EXPECT_THAT(position == read_only, Eq(true));
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
