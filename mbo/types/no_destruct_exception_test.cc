// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <stdexcept>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/types/no_destruct.h"

namespace mbo::types {
namespace {

struct ConditionalThrowingValue {
  ConditionalThrowingValue(int new_value, bool should_throw) : value(new_value) {
    if (should_throw) {
      throw std::runtime_error("requested construction failure");
    }
  }

  int value;
};

struct ThrowingMoveValue {
  ThrowingMoveValue() = default;
  ThrowingMoveValue(const ThrowingMoveValue&) noexcept = default;
  ThrowingMoveValue& operator=(const ThrowingMoveValue&) = default;

  // Inject a caller move exception to verify the wrapper's conditional noexcept contract.
  // NOLINTNEXTLINE(bugprone-unsafe-to-allow-exceptions)
  ThrowingMoveValue(ThrowingMoveValue&& /*other*/) noexcept(false) {
    throw std::runtime_error("requested move failure");
  }

  ThrowingMoveValue& operator=(ThrowingMoveValue&&) noexcept = default;
  ~ThrowingMoveValue() = default;
};

static_assert(!noexcept(NoDestruct<ConditionalThrowingValue>(1, true)));
static_assert(!noexcept(NoDestruct<const ConditionalThrowingValue>(1, true)));
static_assert(!noexcept(NoDestruct<const ThrowingMoveValue>(ThrowingMoveValue{})));

struct NoDestructExceptionTest : ::testing::Test {};

TEST_F(NoDestructExceptionTest, ConstructionSucceedsOrPropagatesException) {
  constexpr int kValue = 42;
  const NoDestruct<ConditionalThrowingValue> value(kValue, false);
  EXPECT_THAT(value->value, kValue);

  bool caught = false;
  try {
    const NoDestruct<ConditionalThrowingValue> throwing_value(kValue, true);
  } catch (const std::runtime_error&) {
    caught = true;
  }

  EXPECT_THAT(caught, true);
}

TEST_F(NoDestructExceptionTest, ConstConstructionSucceedsOrPropagatesException) {
  constexpr int kValue = 42;
  const NoDestruct<const ConditionalThrowingValue> value(kValue, false);
  EXPECT_THAT(value->value, kValue);

  bool caught = false;
  try {
    const NoDestruct<const ConditionalThrowingValue> throwing_value(kValue, true);
  } catch (const std::runtime_error&) {
    caught = true;
  }

  EXPECT_THAT(caught, true);
}

TEST_F(NoDestructExceptionTest, ConstValueConstructionPropagatesMoveException) {
  bool caught = false;
  try {
    const NoDestruct<const ThrowingMoveValue> throwing_value(ThrowingMoveValue{});
  } catch (const std::runtime_error&) {
    caught = true;
  }

  EXPECT_THAT(caught, true);
}

}  // namespace
}  // namespace mbo::types
