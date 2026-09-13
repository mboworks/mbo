// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_packed_node_layout.h"

#include <cstddef>
#include <limits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::Optional;

struct Header final {
  char value;
};

struct alignas(16) Entry final {
  char value;
};

struct alignas(8) Child final {
  char value;
};

using Layout = HamtPackedNodeLayout<Header, Entry, Child>;

struct HamtPackedNodeLayoutTest : ::testing::Test {};

TEST_F(HamtPackedNodeLayoutTest, AlignsDenseDataAndChildArrays) {
  const auto layout = Layout::TryMake(3, 2);
  ASSERT_THAT(layout, Optional(_));
  EXPECT_THAT(layout->data_offset % alignof(Entry), Eq(0));
  EXPECT_THAT(layout->child_offset % alignof(Child), Eq(0));
  EXPECT_THAT(layout->child_offset, Eq(layout->data_offset + 3 * sizeof(Entry)));
  EXPECT_THAT(layout->size, Eq(layout->child_offset + 2 * sizeof(Child)));
  EXPECT_THAT(layout->alignment, Eq(alignof(Entry)));
}

TEST_F(HamtPackedNodeLayoutTest, RejectsArithmeticOverflowBeforeAllocation) {
  EXPECT_THAT(Layout::TryMake(std::numeric_limits<std::size_t>::max(), 0), Eq(std::nullopt));
  EXPECT_THAT(Layout::TryMake(0, std::numeric_limits<std::size_t>::max()), Eq(std::nullopt));
}

TEST_F(HamtPackedNodeLayoutTest, RejectsAlignmentPaddingOverflow) {
  using PaddingLayout = HamtPackedNodeLayout<Header, char, Entry>;
  constexpr auto kMax = std::numeric_limits<std::size_t>::max();
  EXPECT_THAT(PaddingLayout::TryMake(kMax - sizeof(Header), 0), Eq(std::nullopt));
}

TEST_F(HamtPackedNodeLayoutTest, EmptyArraysStillReserveAnAlignedHeader) {
  const auto layout = Layout::TryMake(0, 0);
  ASSERT_THAT(layout, Optional(_));
  EXPECT_THAT(layout->data_offset, Eq(alignof(Entry)));
  EXPECT_THAT(layout->child_offset, Eq(layout->data_offset));
  EXPECT_THAT(layout->size, Eq(layout->child_offset));
}

TEST_F(HamtPackedNodeLayoutTest, AcceptsLargestRepresentableDenseEntryArray) {
  constexpr auto kMax = std::numeric_limits<std::size_t>::max();
  constexpr std::size_t kCount = (kMax - alignof(Entry)) / sizeof(Entry);
  EXPECT_THAT(Layout::TryMake(kCount, 0), Optional(_));
  EXPECT_THAT(Layout::TryMake(kCount + 1, 0), Eq(std::nullopt));
}

TEST_F(HamtPackedNodeLayoutTest, AcceptsLargestRepresentableChildArray) {
  constexpr auto kMax = std::numeric_limits<std::size_t>::max();
  const auto empty = Layout::TryMake(0, 0);
  ASSERT_THAT(empty, Optional(_));
  const std::size_t count = (kMax - empty->child_offset) / sizeof(Child);
  const auto largest = Layout::TryMake(0, count);
  ASSERT_THAT(largest, Optional(_));
  EXPECT_THAT(largest->size, Eq(empty->child_offset + count * sizeof(Child)));
  EXPECT_THAT(Layout::TryMake(0, count + 1), Eq(std::nullopt));
}

TEST_F(HamtPackedNodeLayoutTest, PadsBetweenByteEntriesAndAlignedChildren) {
  using ByteLayout = HamtPackedNodeLayout<Header, char, Child>;
  const auto layout = ByteLayout::TryMake(2, 1);
  ASSERT_THAT(layout, Optional(_));
  EXPECT_THAT(layout->data_offset, Eq(sizeof(Header)));
  EXPECT_THAT(layout->child_offset, Eq(alignof(Child)));
  EXPECT_THAT(layout->size, Eq(alignof(Child) + sizeof(Child)));
}

static_assert(Layout::TryMake(1, 1).has_value());
static_assert(Layout::TryMake(1, 1)->data_offset == 16);
static_assert(Layout::TryMake(1, 1)->child_offset == 32);
static_assert(Layout::TryMake(1, 1)->size == 40);

}  // namespace
}  // namespace mbo::container::container_internal
