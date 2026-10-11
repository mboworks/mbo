// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <stdexcept>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/config/config.h"
#include "mbo/types/optional_data_or_ref.h"
#include "mbo/types/optional_ref.h"

namespace mbo::types {
namespace {
using ::testing::HasSubstr;
using ::testing::ThrowsMessage;

template<typename Optional>
void CheckEmptyAccess() {
  Optional empty;
  const Optional const_empty;
  static_assert(noexcept(empty.value()) == !config::kRequireThrows);
  static_assert(noexcept(const_empty.value()) == !config::kRequireThrows);
  static_assert(noexcept(*empty) == !config::kRequireThrows);
  static_assert(noexcept(*const_empty) == !config::kRequireThrows);
  static_assert(noexcept(empty.operator->()) == !config::kRequireThrows);
  static_assert(noexcept(const_empty.operator->()) == !config::kRequireThrows);
  const auto failure = ThrowsMessage<std::runtime_error>(HasSubstr("No value set for:"));
  EXPECT_THAT([&] { return empty.value(); }, failure);
  EXPECT_THAT([&] { return const_empty.value(); }, failure);
  EXPECT_THAT([&] { return *empty; }, failure);
  EXPECT_THAT([&] { return *const_empty; }, failure);
  EXPECT_THAT([&] { return empty.operator->(); }, failure);
  EXPECT_THAT([&] { return const_empty.operator->(); }, failure);
}

struct OptionalRequireExceptionsTest : ::testing::Test {
 protected:
  void SetUp() override {
    if constexpr (!config::kRequireThrows) {
      GTEST_SKIP() << "Requires require_throws and compiler exceptions";
    }
  }
};

TEST_F(OptionalRequireExceptionsTest, EmptyOptionalRefAccess) {
  CheckEmptyAccess<OptionalRef<int>>();
}

TEST_F(OptionalRequireExceptionsTest, EmptyConstOptionalRefAccess) {
  CheckEmptyAccess<OptionalRef<const int>>();
}

TEST_F(OptionalRequireExceptionsTest, EmptyOptionalDataOrRefAccess) {
  CheckEmptyAccess<OptionalDataOrRef<int>>();
}

TEST_F(OptionalRequireExceptionsTest, EmptyOptionalDataOrConstRefAccess) {
  CheckEmptyAccess<OptionalDataOrConstRef<int>>();
}

TEST_F(OptionalRequireExceptionsTest, EmptyOptionalRefAssignment) {
  OptionalRef<int> empty;
  const int value = 1;
  static_assert(noexcept(empty = 1) == !config::kRequireThrows);
  static_assert(noexcept(empty = value) == !config::kRequireThrows);
  EXPECT_THAT([&] { empty = 1; }, ThrowsMessage<std::runtime_error>(HasSubstr("No value set for:")));
  EXPECT_THAT([&] { empty = value; }, ThrowsMessage<std::runtime_error>(HasSubstr("No value set for:")));
}

TEST_F(OptionalRequireExceptionsTest, EngagedAccessAndAssignment) {
  int value = 1;
  OptionalRef<int> reference(value);
  const OptionalRef<const int> const_reference(value);
  OptionalDataOrRef<int> borrowed(value);
  const OptionalDataOrRef<int> owned(2);
  EXPECT_THAT(reference.value(), 1);
  EXPECT_THAT(*const_reference, 1);
  EXPECT_THAT(borrowed.operator->(), &value);
  EXPECT_THAT(*owned, 2);
  reference = 3;
  EXPECT_THAT(value, 3);
  EXPECT_THAT(*borrowed, 3);
}

struct OptionalRefAssignmentExceptionsTest : ::testing::Test {};

struct ThrowingAssignment {
  ThrowingAssignment() = default;
  ~ThrowingAssignment() = default;
  ThrowingAssignment(const ThrowingAssignment&) = default;
  ThrowingAssignment(ThrowingAssignment&&) = default;

  ThrowingAssignment& operator=(const ThrowingAssignment& other) {
    if (this == &other) {
      return *this;
    }
    throw std::runtime_error("copy assignment");
  }

  ThrowingAssignment& operator=(ThrowingAssignment&& /*other*/) noexcept(false) {
    throw std::runtime_error("move assignment");
  }
};

TEST_F(OptionalRefAssignmentExceptionsTest, PropagatesReferentAssignmentExceptions) {
  ThrowingAssignment value;
  OptionalRef<ThrowingAssignment> reference(value);
  ThrowingAssignment other;
  const ThrowingAssignment const_other;
  static_assert(!noexcept(reference = const_other));
  static_assert(!noexcept(reference = std::move(other)));
  EXPECT_THAT([&] { reference = const_other; }, ThrowsMessage<std::runtime_error>(HasSubstr("copy assignment")));
  EXPECT_THAT([&] { reference = std::move(other); }, ThrowsMessage<std::runtime_error>(HasSubstr("move assignment")));
}
}  // namespace
}  // namespace mbo::types
