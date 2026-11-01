// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/strings/string_id.h"

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::strings {
namespace {
using ::testing::Eq;
using ::testing::Optional;

struct StringIdTest : ::testing::Test {};

struct FirstTag;
struct SecondTag;

template<typename Representation>
struct StringIdLike {
  // NOLINTBEGIN(readability-identifier-naming): Match the public ID aliases.
  using tag_type = FirstTag;
  using value_type = Representation;
  // NOLINTEND(readability-identifier-naming)
};

template<typename T>
concept HasTryIncrement = requires(T identifier) { identifier.try_increment(); };

static_assert(IsStringIdRepresentation<int>);
static_assert(std::is_trivially_copyable_v<StringId<>>);
static_assert(std::is_final_v<StringId<>>);
static_assert(!HasTryIncrement<StringId<>>);
static_assert(sizeof(StringId<std::uint8_t>) == 1);
static_assert(sizeof(StringId<std::uint16_t>) == 2);
static_assert(sizeof(StringId<>) == 8);
static_assert(sizeof(StringId<std::uint64_t>) == 8);
static_assert(sizeof(StringId<std::int32_t>) == 4);
static_assert(StringId<std::uint8_t>{7} == StringId<std::uint8_t>{7});
static_assert(StringId<std::uint8_t>{7} < StringId<std::uint8_t>{8});
static_assert(StringId<std::uint8_t>::try_from_ordinal(254U).has_value());
static_assert(!StringId<std::uint8_t>::try_from_ordinal(255U).has_value());
static_assert(!std::is_convertible_v<StringId<>, std::uint32_t>);
static_assert(StringId<std::uint16_t>::try_from_ordinal(65'534U).has_value());
static_assert(!StringId<std::uint16_t>::try_from_ordinal(65'535U).has_value());
static_assert(StringId<std::uint32_t>::try_from_ordinal(std::uint64_t{0xfffffffe}).has_value());
static_assert(!StringId<std::uint32_t>::try_from_ordinal(std::uint64_t{0xffffffff}).has_value());
static_assert(StringId<std::uint64_t>::try_from_ordinal(std::uint8_t{255}).has_value());

TEST_F(StringIdTest, IsStringIdRecognizesRepresentationsAndCustomTags) {
  static_assert(IsStringId<StringId<>>);
  static_assert(IsStringId<StringId<std::uint8_t>>);
  static_assert(IsStringId<StringId<std::uint64_t>>);
  static_assert(IsStringId<StringId<std::int32_t>>);
  static_assert(IsStringId<StringId<std::uint32_t, FirstTag>, FirstTag>);
  static_assert(IsStringId<StringId<std::uint32_t, SecondTag>, SecondTag>);
}

TEST_F(StringIdTest, IsStringIdChecksAnExplicitTagAndTreatsVoidAsUnrestricted) {
  using Id = StringId<std::uint32_t, FirstTag>;
  static_assert(IsStringId<Id, FirstTag>);
  static_assert(IsStringId<Id, void>);
  static_assert(IsStringId<StringId<>, StringId<>::tag_type>);
  static_assert(!IsStringId<Id>);
  static_assert(!IsStringId<Id, SecondTag>);
  static_assert(!IsStringId<StringId<>, FirstTag>);
}

TEST_F(StringIdTest, IsStringIdRejectsUnrelatedAndLookalikeTypes) {
  static_assert(!IsStringId<void>);
  static_assert(!IsStringId<int>);
  static_assert(!IsStringId<FirstTag>);
  static_assert(!IsStringId<StringIdLike<std::uint32_t>>);
  static_assert(!IsStringId<StringIdLike<std::uint32_t>, FirstTag>);
  static_assert(!IsStringId<StringIdLike<bool>>);
  static_assert(!IsStringId<StringIdLike<double>>);
}

TEST_F(StringIdTest, IsStringIdRequiresAnUnqualifiedValueType) {
  using Id = StringId<>;
  static_assert(!IsStringId<const Id>);
  static_assert(!IsStringId<volatile Id>);
  static_assert(!IsStringId<const volatile Id>);
  static_assert(!IsStringId<Id&>);
  static_assert(!IsStringId<const Id&>);
  static_assert(!IsStringId<Id&&>);
  static_assert(!IsStringId<Id*>);
}

TEST_F(StringIdTest, DefaultIsInvalidAndDenseOrdinalsStartAtZero) {
  using Id = StringId<std::uint8_t>;
  static_assert(std::same_as<decltype(Id{}.value()), Id::value_type>);
  EXPECT_THAT(Id{}.value(), Eq(Id::invalid_value));
  EXPECT_THAT(Id{}.is_valid(), Eq(false));
  EXPECT_THAT(Id{0}.is_valid(), Eq(true));
  EXPECT_THAT(Id::try_from_ordinal(254U).value_or(Id{}).value(), Eq(254));
  EXPECT_THAT(Id::try_from_ordinal(255U).has_value(), Eq(false));
  EXPECT_THAT(Id::try_from_ordinal(std::numeric_limits<std::uint64_t>::max()).has_value(), Eq(false));
  EXPECT_THAT(
      StringId<std::uint64_t>::try_from_ordinal(std::numeric_limits<std::uint64_t>::max()).has_value(), Eq(false));
}

TEST_F(StringIdTest, SignedRepresentationsPermitNegativeKnownValues) {
  using Id = StringId<std::int8_t>;
  EXPECT_THAT(Id{-1}.is_valid(), Eq(true));
  EXPECT_THAT(Id::try_from_ordinal(-1), Optional(Id{-1}));
  EXPECT_THAT(Id::try_from_ordinal(126), Optional(Id{126}));
  EXPECT_THAT(Id::try_from_ordinal(127), Eq(std::nullopt));
}
}  // namespace
}  // namespace mbo::strings
