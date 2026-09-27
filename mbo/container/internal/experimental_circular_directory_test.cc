// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/experimental_circular_directory.h"

#include <cstddef>
#include <limits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::Eq;
using ::testing::IsNull;

struct ExperimentalCircularDirectoryTest : ::testing::Test {};

constexpr bool ConstantEvaluation() {
  int first = 1;
  int second = 2;
  ExperimentalCircularDirectory<int*> directory;
  directory.Grow(2, 8, 0, 0);
  directory.At(std::numeric_limits<std::size_t>::max()) = &first;
  directory.At(0) = &second;
  directory.Grow(3, 8, std::numeric_limits<std::size_t>::max(), 2);
  return directory.At(std::numeric_limits<std::size_t>::max()) == &first && directory.At(0) == &second
         && directory.SlotCount() == 4 && directory.At(1) == nullptr;
}

static_assert(ConstantEvaluation());

TEST_F(ExperimentalCircularDirectoryTest, WrapAndGrowthPreservePointerIdentity) {
  EXPECT_THAT(ConstantEvaluation(), Eq(true));
}

TEST_F(ExperimentalCircularDirectoryTest, ReservationAndClearingOwnOnlyPointerStorage) {
  int value = 42;
  ExperimentalCircularDirectory<int*> directory;
  directory.Grow(0, 8, 0, 0);
  EXPECT_THAT(directory.SlotCount(), Eq(0));
  directory.Grow(8, 8, 0, 0);
  EXPECT_THAT(directory.At(0), IsNull());
  directory.At(9) = &value;
  directory.Grow(2, 8, 9, 1);
  EXPECT_THAT(directory.At(1), Eq(&value));
  directory.Clear();
  EXPECT_THAT(directory.SlotCount(), Eq(0));
  EXPECT_THAT(value, Eq(42));
}

}  // namespace
}  // namespace mbo::container::container_internal
