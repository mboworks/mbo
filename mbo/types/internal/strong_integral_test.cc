// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "mbo/types/internal/strong_integral.h"

#include <cstdint>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::types::types_internal {
namespace {
using ::testing::Eq;

struct ValueTag;

class Value final : public StrongIntegralBase<Value, ValueTag, std::int8_t, 7> {
 private:
  using Base = StrongIntegralBase<Value, ValueTag, std::int8_t, 7>;

 public:
  constexpr Value() noexcept = default;

  explicit constexpr Value(value_type value) noexcept : Base(value) {}

  constexpr Value(const Value&) noexcept = default;
  constexpr Value(Value&&) noexcept = default;
  constexpr Value& operator=(const Value&) = delete;
  constexpr Value& operator=(Value&&) noexcept = default;
  ~Value() = default;
};

struct StrongIntegralTest : ::testing::Test {};

static_assert(StrongIntegralRepresentation<std::int8_t>);
static_assert(!StrongIntegralRepresentation<bool>);
static_assert(std::is_trivially_copyable_v<Value>);
static_assert(!std::is_copy_assignable_v<Value>);
static_assert(std::is_move_assignable_v<Value>);
static_assert(sizeof(Value) == sizeof(std::int8_t));

TEST_F(StrongIntegralTest, SuppliesOnlyTaggedValueMechanics) {
  Value value;
  EXPECT_THAT(value.value(), Eq(7));
  EXPECT_THAT(Value{3} < Value{4}, Eq(true));
  value = Value{9};
  EXPECT_THAT(value.value(), Eq(9));
}

}  // namespace
}  // namespace mbo::types::types_internal
