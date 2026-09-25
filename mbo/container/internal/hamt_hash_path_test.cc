// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/container/internal/hamt_hash_path.h"

#include <cstddef>
#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::container::container_internal {
namespace {

using ::testing::Eq;

struct HamtHashPathTest : ::testing::Test {
  template<std::size_t Bits, typename Hash>
  static void RecoversHash(Hash hash) {
    const HamtHashPath<Hash, Bits> path(hash);
    Hash recovered = 0;
    for (std::size_t level = 0; level < path.kLevels; ++level) {
      recovered |= static_cast<Hash>(path.Fragment(level)) << (level * Bits);
    }
    EXPECT_THAT(recovered, Eq(hash));
    EXPECT_THAT(path.HashValue(), Eq(hash));
  }
};

TEST_F(HamtHashPathTest, TraversesAllCandidateWidthsAndHashSizes) {
  constexpr std::uint32_t kHash32 = 0xfedcba98U;
  constexpr std::uint64_t kHash64 = 0xfedcba9876543210ULL;
  RecoversHash<4>(kHash32);
  RecoversHash<5>(kHash32);
  RecoversHash<6>(kHash32);
  RecoversHash<7>(kHash32);
  RecoversHash<4>(kHash64);
  RecoversHash<5>(kHash64);
  RecoversHash<6>(kHash64);
  RecoversHash<7>(kHash64);
}

static_assert(HamtHashPath<std::uint32_t, 5>::kLevels == 7);
static_assert(HamtHashPath<std::uint64_t, 7>::kLevels == 10);
static_assert(HamtHashPath<std::uint32_t, 7>(0xf0000000U).Fragment(4) == 15);

}  // namespace
}  // namespace mbo::container::container_internal
