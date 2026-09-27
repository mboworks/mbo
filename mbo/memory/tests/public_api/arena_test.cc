// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/memory/arena.h"

#include <cstddef>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::memory {
namespace {

using ::testing::Eq;
using ::testing::NotNull;

struct ArenaPublicApiTest : ::testing::Test {};

TEST_F(ArenaPublicApiTest, CheckpointAllocationIsUsableOutsideItsDefiningPackage) {
  Arena<
      NewDeleteBlockSource,
      ArenaOptions{
          .initial_block_size = 256, .maximum_block_size = 1'024, .growth_numerator = 2, .growth_denominator = 1}>
      arena;
  EXPECT_THAT(arena.Allocate(3, alignof(std::max_align_t)), NotNull());
  const auto checkpoint = arena.checkpoint();
  auto* const before_rewind = arena.Allocate(11);
  arena.rewind(checkpoint);
  EXPECT_THAT(arena.Allocate(11), Eq(before_rewind));
}

}  // namespace
}  // namespace mbo::memory
