// SPDX-FileCopyrightText: Copyright (c) M. Boerger, The MBO Works Authors
// SPDX-License-Identifier: Apache-2.0

#include <stdexcept>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/config/config.h"
#include "mbo/container/limited_map.h"

namespace mbo::container {
namespace {

using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::ThrowsMessage;

struct LimitedMapRequireExceptionsTest : ::testing::Test {};

struct ThrowingMapped final {
  static inline int live = 0;
  static inline int copies_before_throw = -1;
  static inline int moves_before_throw = -1;

  explicit ThrowingMapped(int value_arg) : value(value_arg) { ++live; }

  ThrowingMapped(const ThrowingMapped& other) : value(other.value) {
    if (copies_before_throw == 0) {
      throw std::runtime_error("copy failed");
    }
    if (copies_before_throw > 0) {
      --copies_before_throw;
    }
    ++live;
  }

  // Deliberately throwing move construction exercises container rollback.
  // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor)
  ThrowingMapped(ThrowingMapped&& other) : value(other.value) {
    if (moves_before_throw == 0) {
      throw std::runtime_error("move failed");
    }
    if (moves_before_throw > 0) {
      --moves_before_throw;
    }
    ++live;
  }

  ThrowingMapped& operator=(const ThrowingMapped&) = default;
  ThrowingMapped& operator=(ThrowingMapped&&) = default;

  ~ThrowingMapped() { --live; }

  int value;
};

struct ThrowingLess final {
  constexpr bool operator()(int lhs, int rhs) const {
    static_cast<void>(lhs);
    static_cast<void>(rhs);
    throw std::runtime_error("compare failed");
  }
};

TEST_F(LimitedMapRequireExceptionsTest, ReportsMissingKey) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  LimitedMap<int, int, 1> map{{1, 2}};
  EXPECT_THAT([&map] { static_cast<void>(map.at(2)); }, ThrowsMessage<std::runtime_error>(HasSubstr("Out of range")));
}

TEST_F(LimitedMapRequireExceptionsTest, RelocationFailureLeavesValidEmptyMap) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  EXPECT_THAT(ThrowingMapped::live, 0);
  {
    LimitedMap<int, ThrowingMapped, 3> map;
    map.try_emplace(1, 10);
    map.try_emplace(3, 30);
    ThrowingMapped::moves_before_throw = 0;

    EXPECT_THAT([&map] { map.try_emplace(2, 20); }, ThrowsMessage<std::runtime_error>(HasSubstr("move failed")));
    EXPECT_THAT(map, IsEmpty());
    EXPECT_THAT(ThrowingMapped::live, 0);
    ThrowingMapped::moves_before_throw = -1;
    map.try_emplace(4, 40);
  }
  EXPECT_THAT(ThrowingMapped::live, 0);
}

TEST_F(LimitedMapRequireExceptionsTest, PartialCopyConstructionDestroysConstructedElements) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  EXPECT_THAT(ThrowingMapped::live, 0);
  {
    LimitedMap<int, ThrowingMapped, 3> source;
    source.try_emplace(1, 10);
    source.try_emplace(2, 20);
    ThrowingMapped::copies_before_throw = 1;

    EXPECT_THAT(
        [&source] { static_cast<void>(LimitedMap<int, ThrowingMapped, 3>(source)); },
        ThrowsMessage<std::runtime_error>(HasSubstr("copy failed")));
    EXPECT_THAT(ThrowingMapped::live, 2);
    ThrowingMapped::copies_before_throw = -1;
  }
  EXPECT_THAT(ThrowingMapped::live, 0);
}

TEST_F(LimitedMapRequireExceptionsTest, LaterRightShiftFailureDestroysAllLiveElements) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  EXPECT_THAT(ThrowingMapped::live, 0);
  {
    LimitedMap<int, ThrowingMapped, 4> map;
    map.try_emplace(1, 10);
    map.try_emplace(3, 30);
    map.try_emplace(5, 50);
    ThrowingMapped::moves_before_throw = 1;

    EXPECT_THAT([&map] { map.try_emplace(2, 20); }, ThrowsMessage<std::runtime_error>(HasSubstr("move failed")));
    EXPECT_THAT(map, IsEmpty());
    EXPECT_THAT(ThrowingMapped::live, 0);
    ThrowingMapped::moves_before_throw = -1;
  }
  EXPECT_THAT(ThrowingMapped::live, 0);
}

TEST_F(LimitedMapRequireExceptionsTest, LaterLeftShiftFailureDestroysAllLiveElements) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  EXPECT_THAT(ThrowingMapped::live, 0);
  {
    LimitedMap<int, ThrowingMapped, 3> map;
    map.try_emplace(1, 10);
    map.try_emplace(3, 30);
    map.try_emplace(5, 50);
    ThrowingMapped::moves_before_throw = 1;

    EXPECT_THAT([&map] { map.erase(map.begin()); }, ThrowsMessage<std::runtime_error>(HasSubstr("move failed")));
    EXPECT_THAT(map, IsEmpty());
    EXPECT_THAT(ThrowingMapped::live, 0);
    ThrowingMapped::moves_before_throw = -1;
  }
  EXPECT_THAT(ThrowingMapped::live, 0);
}

TEST_F(LimitedMapRequireExceptionsTest, ComparatorFailurePropagates) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  LimitedMap<int, int, 2, ThrowingLess> map;
  map.try_emplace(1, 10);

  EXPECT_THAT([&map] { static_cast<void>(map.contains(1)); }, ThrowsMessage<std::runtime_error>(HasSubstr("compare")));
}

}  // namespace
}  // namespace mbo::container
