// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/string_id.h"

#include <cstdint>
#include <limits>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::strings {
namespace {
using ::testing::Eq;

struct StringIdTest : ::testing::Test {};

static_assert(!StringIdRepresentation<int>);
static_assert(std::is_trivially_copyable_v<StringId<>>);
static_assert(sizeof(StringId<std::uint8_t>) == 1);
static_assert(sizeof(StringId<std::uint16_t>) == 2);
static_assert(sizeof(StringId<>) == 4);
static_assert(sizeof(StringId<std::uint64_t>) == 8);
static_assert(StringId<std::uint8_t>::TryFromOrdinal(255U).has_value());
static_assert(!StringId<std::uint8_t>::TryFromOrdinal(256U).has_value());
static_assert(!std::is_convertible_v<StringId<>, std::uint32_t>);
static_assert(StringId<std::uint16_t>::TryFromOrdinal(65'535U).has_value());
static_assert(!StringId<std::uint16_t>::TryFromOrdinal(65'536U).has_value());
static_assert(StringId<std::uint32_t>::TryFromOrdinal(std::uint64_t{0xffffffff}).has_value());
static_assert(!StringId<std::uint32_t>::TryFromOrdinal(std::uint64_t{0x100000000}).has_value());
static_assert(StringId<std::uint64_t>::TryFromOrdinal(std::uint8_t{255}).has_value());

TEST_F(StringIdTest, ZeroAndMaximumAreValidAndExhaustionIsSeparate) {
  using Id = StringId<std::uint8_t>;
  EXPECT_THAT(Id{}.value(), Eq(0));
  EXPECT_THAT(Id::TryFromOrdinal(255U).value_or(Id{}).value(), Eq(255));
  EXPECT_THAT(Id::TryFromOrdinal(256U).has_value(), Eq(false));
  EXPECT_THAT(Id::TryFromOrdinal(std::numeric_limits<std::uint64_t>::max()).has_value(), Eq(false));
  EXPECT_THAT(StringId<std::uint64_t>::TryFromOrdinal(std::numeric_limits<std::uint64_t>::max()).has_value(), Eq(true));
}
}  // namespace
}  // namespace mbo::strings
