// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/types/strong_id.h"

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <unordered_set>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::types {
namespace {
using ::testing::Eq;

struct FirstTag;
struct SecondTag;

using Id = ConstStrongId<FirstTag, std::uint8_t>;
using CustomSentinelId = ConstStrongId<FirstTag, std::uint8_t, 0>;
using DefaultId = ConstStrongId<FirstTag>;

template<typename T>
concept HasTryAdd = requires(T value) { value.try_add(1); };

struct StrongIdTest : ::testing::Test {};

static_assert(StrongIdRepresentation<unsigned int>);
static_assert(!StrongIdRepresentation<bool>);
static_assert(!StrongIdRepresentation<int>);
static_assert(!std::same_as<Id, ConstStrongId<SecondTag, std::uint8_t>>);
static_assert(std::is_trivially_copyable_v<Id>);
static_assert(std::is_final_v<Id>);
static_assert(!std::is_copy_assignable_v<Id>);
static_assert(std::is_move_assignable_v<Id>);
static_assert(sizeof(Id) == sizeof(std::uint8_t));
static_assert(std::same_as<DefaultId::value_type, std::uint32_t>);
static_assert(Id::default_value == Id::invalid_value);
static_assert(!HasTryAdd<Id>);

TEST_F(StrongIdTest, DefaultsToInvalidAndRejectsItsSentinel) {
  EXPECT_THAT(Id{}.value(), Eq(Id::invalid_value));
  EXPECT_THAT(Id{}.is_valid(), Eq(false));
  EXPECT_THAT(Id{0}.is_valid(), Eq(true));
  EXPECT_THAT(Id::try_from_ordinal(-1).has_value(), Eq(false));
  EXPECT_THAT(Id::try_from_ordinal(254).has_value(), Eq(true));
  EXPECT_THAT(Id::try_from_ordinal(255).has_value(), Eq(false));
}

TEST_F(StrongIdTest, SupportsAnExplicitInvalidSentinel) {
  EXPECT_THAT(CustomSentinelId{}.value(), Eq(0));
  EXPECT_THAT(CustomSentinelId{}.is_valid(), Eq(false));
  EXPECT_THAT(CustomSentinelId{1}.is_valid(), Eq(true));
  EXPECT_THAT(CustomSentinelId::try_from_ordinal(0).has_value(), Eq(false));
  EXPECT_THAT(CustomSentinelId::try_from_ordinal(1).has_value(), Eq(true));
}

TEST_F(StrongIdTest, SupportsStandardHashContainers) {
  const std::unordered_set<Id> ids = {Id{1}, Id{2}};
  EXPECT_THAT(ids.contains(Id{1}), Eq(true));
  EXPECT_THAT(ids.contains(Id{3}), Eq(false));
}

}  // namespace
}  // namespace mbo::types
