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
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Field;
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
  constexpr std::size_t kChildOffset = alignof(Entry) + (3 * sizeof(Entry));
  EXPECT_THAT(
      Layout::TryMake(3, 2), Optional(AllOf(
                                 Field("data_offset", &Layout::data_offset, Eq(alignof(Entry))),
                                 Field("child_offset", &Layout::child_offset, Eq(kChildOffset)),
                                 Field("size", &Layout::size, Eq(kChildOffset + (2 * sizeof(Child)))),
                                 Field("alignment", &Layout::alignment, Eq(alignof(Entry))))));
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
  EXPECT_THAT(
      Layout::TryMake(0, 0), Optional(AllOf(
                                 Field("data_offset", &Layout::data_offset, Eq(alignof(Entry))),
                                 Field("child_offset", &Layout::child_offset, Eq(alignof(Entry))),
                                 Field("size", &Layout::size, Eq(alignof(Entry))))));
}

TEST_F(HamtPackedNodeLayoutTest, AcceptsLargestRepresentableDenseEntryArray) {
  constexpr auto kMax = std::numeric_limits<std::size_t>::max();
  constexpr std::size_t kCount = (kMax - alignof(Entry)) / sizeof(Entry);
  EXPECT_THAT(Layout::TryMake(kCount, 0), Optional(_));
  EXPECT_THAT(Layout::TryMake(kCount + 1, 0), Eq(std::nullopt));
}

TEST_F(HamtPackedNodeLayoutTest, AcceptsLargestRepresentableChildArray) {
  constexpr auto kMax = std::numeric_limits<std::size_t>::max();
  constexpr std::size_t kCount = (kMax - alignof(Entry)) / sizeof(Child);
  EXPECT_THAT(
      Layout::TryMake(0, kCount),
      Optional(Field("size", &Layout::size, Eq(alignof(Entry) + (kCount * sizeof(Child))))));
  EXPECT_THAT(Layout::TryMake(0, kCount + 1), Eq(std::nullopt));
}

TEST_F(HamtPackedNodeLayoutTest, PadsBetweenByteEntriesAndAlignedChildren) {
  using ByteLayout = HamtPackedNodeLayout<Header, char, Child>;
  EXPECT_THAT(
      ByteLayout::TryMake(2, 1), Optional(AllOf(
                                     Field("data_offset", &ByteLayout::data_offset, Eq(sizeof(Header))),
                                     Field("child_offset", &ByteLayout::child_offset, Eq(alignof(Child))),
                                     Field("size", &ByteLayout::size, Eq(alignof(Child) + sizeof(Child))))));
}

static_assert(Layout::TryMake(1, 1).has_value());
static_assert(Layout::TryMake(1, 1)->data_offset == 16);
static_assert(Layout::TryMake(1, 1)->child_offset == 32);
static_assert(Layout::TryMake(1, 1)->size == 40);

}  // namespace
}  // namespace mbo::container::container_internal
