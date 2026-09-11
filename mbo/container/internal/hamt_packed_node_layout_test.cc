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

static_assert(Layout::TryMake(1, 1).has_value());

}  // namespace
}  // namespace mbo::container::container_internal
