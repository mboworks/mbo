// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/types/strong_ordinal.h"

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <unordered_set>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::types {
namespace {
using ::testing::Eq;
using ::testing::Optional;

struct ConstTag;
struct MutableTag;
struct OtherTag;

using ConstOrdinal = ConstStrongOrdinal<ConstTag, std::int8_t>;
using MutableOrdinal = StrongOrdinal<MutableTag, std::int8_t>;
using UnsignedOrdinal = StrongOrdinal<MutableTag, std::uint8_t>;

struct StrongOrdinalTest : ::testing::Test {};

static_assert(StrongOrdinalRepresentation<unsigned int>);
static_assert(StrongOrdinalRepresentation<int>);
static_assert(!StrongOrdinalRepresentation<bool>);
static_assert(std::is_trivially_copyable_v<ConstOrdinal>);
static_assert(!std::is_copy_assignable_v<ConstOrdinal>);
static_assert(std::is_move_assignable_v<ConstOrdinal>);
static_assert(std::is_copy_assignable_v<MutableOrdinal>);
static_assert(std::is_move_assignable_v<MutableOrdinal>);
static_assert(sizeof(ConstOrdinal) == sizeof(std::int8_t));
static_assert(!std::same_as<ConstOrdinal, ConstStrongOrdinal<OtherTag, std::int8_t>>);
static_assert(ConstOrdinal::default_value == 0);
static_assert(ConstOrdinal{7} == ConstOrdinal{7});
static_assert(ConstOrdinal{7} < ConstOrdinal{8});
static_assert(MutableOrdinal{7} == MutableOrdinal{7});
static_assert(ConstOrdinal::try_from_ordinal(127).has_value());
static_assert(!ConstOrdinal::try_from_ordinal(128).has_value());

constexpr bool SupportsMutableOperations() {
  MutableOrdinal id{7};
  if (!id.try_set(8) || id.value() != 8) {
    return false;
  }
  id.set(9);
  if (id.value() != 9 || ++id != MutableOrdinal{10}) {
    return false;
  }
  if (id++ != MutableOrdinal{10} || id.value() != 11) {
    return false;
  }
  if (--id != MutableOrdinal{10} || id-- != MutableOrdinal{10}) {
    return false;
  }
  id = 12;
  id += 4;
  if (id != MutableOrdinal{16} || id + 3 != MutableOrdinal{19}) {
    return false;
  }
  id -= 5;
  return id == MutableOrdinal{11} && id - 4 == MutableOrdinal{7};
}

static_assert(SupportsMutableOperations());

TEST_F(StrongOrdinalTest, ConstOrdinalDefaultsToZero) {
  EXPECT_THAT(ConstOrdinal{}.value(), Eq(0));
  EXPECT_THAT(ConstOrdinal{0}.value(), Eq(0));
}

TEST_F(StrongOrdinalTest, ConstOrdinalCheckedOperationsReportUnrepresentableResults) {
  EXPECT_THAT(ConstOrdinal::try_from_ordinal(127), Optional(Eq(ConstOrdinal{127})));
  EXPECT_THAT(ConstOrdinal::try_from_ordinal(128).has_value(), Eq(false));
  EXPECT_THAT(ConstOrdinal{127}.try_add(1).has_value(), Eq(false));
  EXPECT_THAT(ConstOrdinal{-128}.try_subtract(1).has_value(), Eq(false));
}

TEST_F(StrongOrdinalTest, MutableOrdinalChangesOnlyWithinValidRange) {
  MutableOrdinal lowest{-128};
  EXPECT_THAT(lowest.try_decrement(), Eq(false));
  EXPECT_THAT(lowest.value(), Eq(-128));

  MutableOrdinal id{0};
  EXPECT_THAT(id.try_increment(), Eq(true));
  EXPECT_THAT(id.value(), Eq(1));
  EXPECT_THAT(id.try_decrement(), Eq(true));
  EXPECT_THAT(id.value(), Eq(0));

  MutableOrdinal last{127};
  EXPECT_THAT(last.try_increment(), Eq(false));
  EXPECT_THAT(last.value(), Eq(127));
}

TEST_F(StrongOrdinalTest, MutableOrdinalSupportsCheckedAssignmentAndOperators) {
  MutableOrdinal id{7};
  EXPECT_THAT(id.try_set(8), Eq(true));
  EXPECT_THAT(id.value(), Eq(8));
  EXPECT_THAT(id.try_set(-129), Eq(false));
  EXPECT_THAT(id.try_set(128), Eq(false));
  EXPECT_THAT(id.value(), Eq(8));

  id.set(9);
  EXPECT_THAT(id.value(), Eq(9));
  id = 10;
  EXPECT_THAT(id.value(), Eq(10));

  EXPECT_THAT((++id).value(), Eq(11));
  EXPECT_THAT((id++).value(), Eq(11));
  EXPECT_THAT(id.value(), Eq(12));
  EXPECT_THAT((--id).value(), Eq(11));
  EXPECT_THAT((id--).value(), Eq(11));
  EXPECT_THAT(id.value(), Eq(10));
}

TEST_F(StrongOrdinalTest, MutableOrdinalSupportsCheckedOrdinalArithmetic) {
  MutableOrdinal id{10};
  EXPECT_THAT(id.try_add_assign(5), Eq(true));
  EXPECT_THAT(id.value(), Eq(15));
  EXPECT_THAT(id.try_subtract_assign(3), Eq(true));
  EXPECT_THAT(id.value(), Eq(12));
  EXPECT_THAT(id.try_add_assign(-1), Eq(false));
  EXPECT_THAT(id.try_subtract_assign(-1), Eq(false));
  EXPECT_THAT(id.try_add_assign(128), Eq(false));
  EXPECT_THAT(id.try_subtract_assign(141), Eq(false));
  EXPECT_THAT(id.value(), Eq(12));

  id += 8;
  EXPECT_THAT(id.value(), Eq(20));
  EXPECT_THAT((id + 4).value(), Eq(24));
  EXPECT_THAT(id.value(), Eq(20));
  id -= 5;
  EXPECT_THAT(id.value(), Eq(15));
  EXPECT_THAT((id - 4).value(), Eq(11));
  EXPECT_THAT(id.value(), Eq(15));
}

TEST_F(StrongOrdinalTest, ConstOrdinalSupportsNonMutatingArithmetic) {
  const ConstOrdinal ordinal{-10};
  EXPECT_THAT(ordinal.try_add(4), Optional(Eq(ConstOrdinal{-6})));
  EXPECT_THAT(ordinal.try_subtract(4), Optional(Eq(ConstOrdinal{-14})));
  EXPECT_THAT((ordinal + 4).value(), Eq(-6));
  EXPECT_THAT((ordinal - 4).value(), Eq(-14));
  EXPECT_THAT(ordinal.value(), Eq(-10));
}

TEST_F(StrongOrdinalTest, SignedOrdinalArithmeticChecksBothRepresentationBounds) {
  MutableOrdinal lowest{-128};
  EXPECT_THAT(lowest.try_decrement(), Eq(false));
  EXPECT_THAT(lowest.try_subtract_assign(1), Eq(false));
  EXPECT_THAT(lowest.value(), Eq(-128));

  MutableOrdinal negative{-2};
  EXPECT_THAT(negative.try_add_assign(1), Eq(true));
  EXPECT_THAT(negative.value(), Eq(-1));
  EXPECT_THAT(negative.try_subtract_assign(2), Eq(true));
  EXPECT_THAT(negative.value(), Eq(-3));
}

TEST_F(StrongOrdinalTest, ArithmeticChecksTheResultRatherThanTheOffsetRepresentation) {
  MutableOrdinal from_lowest{-128};
  EXPECT_THAT(from_lowest.try_add_assign(std::uint16_t{200}), Eq(true));
  EXPECT_THAT(from_lowest.value(), Eq(72));

  MutableOrdinal from_highest{127};
  EXPECT_THAT(from_highest.try_subtract_assign(std::uint16_t{200}), Eq(true));
  EXPECT_THAT(from_highest.value(), Eq(-73));

  EXPECT_THAT(from_lowest.try_add_assign(std::uint16_t{256}), Eq(false));
  EXPECT_THAT(from_lowest.value(), Eq(72));
  EXPECT_THAT(from_highest.try_subtract_assign(std::uint16_t{256}), Eq(false));
  EXPECT_THAT(from_highest.value(), Eq(-73));
}

TEST_F(StrongOrdinalTest, UnsignedOrdinalChecksBothRepresentationBounds) {
  UnsignedOrdinal first{0};
  EXPECT_THAT(first.try_decrement(), Eq(false));
  EXPECT_THAT(first.value(), Eq(0));

  UnsignedOrdinal last{255};
  EXPECT_THAT(last.try_increment(), Eq(false));
  EXPECT_THAT(last.value(), Eq(255));
  EXPECT_THAT(last.try_subtract_assign(std::uint16_t{255}), Eq(true));
  EXPECT_THAT(last.value(), Eq(0));
}

TEST_F(StrongOrdinalTest, OrdinalsSupportStandardHashContainers) {
  const std::unordered_set<ConstOrdinal> ordinals = {ConstOrdinal{-1}, ConstOrdinal{2}};
  EXPECT_THAT(ordinals.contains(ConstOrdinal{-1}), Eq(true));
  EXPECT_THAT(ordinals.contains(ConstOrdinal{3}), Eq(false));
}

}  // namespace
}  // namespace mbo::types
