// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <stdexcept>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/types/optional_data_or_ref.h"

namespace mbo::types {
namespace {

using ::testing::Eq;

struct ThrowingValue {
  explicit ThrowingValue(int new_value) : value(new_value) { MaybeThrow(); }

  ThrowingValue(const ThrowingValue& other) : value(other.value) { MaybeThrow(); }

  // This move injects a caller exception to test the wrapper's exception guarantee.
  // NOLINTNEXTLINE(bugprone-unsafe-to-allow-exceptions,performance-noexcept-move-constructor)
  ThrowingValue(ThrowingValue&& other) : value(other.value) { MaybeThrow(); }

  ThrowingValue& operator=(const ThrowingValue&) = default;
  ThrowingValue& operator=(ThrowingValue&&) = default;
  ~ThrowingValue() = default;

  static void MaybeThrow() {
    if (throw_on_construction) {
      throw std::runtime_error("requested construction failure");
    }
  }

  static inline bool throw_on_construction = false;
  int value;
};

template<typename Func>
bool ThrowsRuntimeError(Func&& func) {
  try {
    std::forward<Func>(func)();
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

static_assert(!noexcept(std::declval<OptionalDataOrRef<ThrowingValue>&>().emplace(1)));
static_assert(!noexcept(OptionalDataOrRef<ThrowingValue>(ThrowingValue{1})));

TEST(OptionalDataOrRefExceptionTest, FailedEmplaceLeavesValidEmptyState) {
  OptionalDataOrRef<ThrowingValue> ref(ThrowingValue{1});
  ThrowingValue::throw_on_construction = true;
  const bool caught = ThrowsRuntimeError([&ref] { ref.emplace(2); });
  ThrowingValue::throw_on_construction = false;

  EXPECT_THAT(caught, true);
  EXPECT_THAT(ref.HoldsNullopt(), true);
  ref.emplace(3);
  EXPECT_THAT(ref.HoldsData(), true);
  EXPECT_THAT(ref->value, Eq(3));
}

TEST(OptionalDataOrRefExceptionTest, FailedCopyAndReferencePromotionLeaveValidEmptyState) {
  const OptionalDataOrRef<ThrowingValue> source(ThrowingValue{1});
  OptionalDataOrRef<ThrowingValue> target(ThrowingValue{2});
  ThrowingValue::throw_on_construction = true;
  const bool copy_caught = ThrowsRuntimeError([&source, &target] { target = source; });
  ThrowingValue::throw_on_construction = false;
  EXPECT_THAT(copy_caught, true);
  EXPECT_THAT(source.HoldsData(), true);
  EXPECT_THAT(target.HoldsNullopt(), true);

  ThrowingValue borrowed(3);
  OptionalDataOrRef<ThrowingValue> reference(borrowed);
  ThrowingValue::throw_on_construction = true;
  const bool promotion_caught = ThrowsRuntimeError([&reference] { reference.as_data(4); });
  ThrowingValue::throw_on_construction = false;
  EXPECT_THAT(promotion_caught, true);
  EXPECT_THAT(reference.HoldsNullopt(), true);
  EXPECT_THAT(borrowed.value, Eq(3));
}

}  // namespace
}  // namespace mbo::types
