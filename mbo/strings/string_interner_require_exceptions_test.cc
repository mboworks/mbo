// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <stdexcept>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/config/config.h"
#include "mbo/strings/string_interner.h"
#include "mbo/strings/string_interner_map.h"

namespace mbo::strings {
namespace {

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;

struct StringInternerRequireExceptionsTest : ::testing::Test {};

static_assert(noexcept(*std::declval<StringInterner<>::iterator&>()) == !::mbo::config::kRequireThrows);
static_assert(noexcept(++std::declval<StringInterner<>::iterator&>()) == !::mbo::config::kRequireThrows);
static_assert(noexcept(--std::declval<StringInterner<>::iterator&>()) == !::mbo::config::kRequireThrows);
static_assert(noexcept(std::declval<StringInterner<>&>().intern(std::string_view{})) == !::mbo::config::kRequireThrows);
static_assert(
    noexcept(std::declval<StringInterner<>&>().intern_parent_first(std::string_view{}))
    == !::mbo::config::kRequireThrows);
static_assert(
    noexcept(std::declval<StringInterner<>&>().intern_child_first(std::string_view{}))
    == !::mbo::config::kRequireThrows);
static_assert(
    noexcept(std::declval<StringInterner<>&>().try_intern(std::string_view{})) == !::mbo::config::kRequireThrows);
static_assert(
    noexcept(std::declval<StringInterner<>&>().try_intern_id(std::string_view{})) == !::mbo::config::kRequireThrows);
static_assert(
    noexcept(std::declval<StringInternerMap<int>&>().try_emplace(std::string_view{}, 0))
    == !::mbo::config::kRequireThrows);
static_assert(noexcept(*std::declval<StringInternerMap<int>::iterator&>()) == !::mbo::config::kRequireThrows);
static_assert(noexcept(++std::declval<StringInternerMap<int>::iterator&>()) == !::mbo::config::kRequireThrows);
static_assert(noexcept(--std::declval<StringInternerMap<int>::iterator&>()) == !::mbo::config::kRequireThrows);

TEST_F(StringInternerRequireExceptionsTest, InvalidIteratorOperationsThrow) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  StringInterner<> interner;
  const StringInterner<>::iterator singular;
  EXPECT_THAT(
      [&singular] { static_cast<void>(*singular); },
      ThrowsMessage<std::runtime_error>(HasSubstr("singular StringInterner iterator")));
  EXPECT_THAT(
      [&interner] { static_cast<void>(++interner.end()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("StringInterner end iterator")));
  EXPECT_THAT(
      [&interner] { static_cast<void>(--interner.begin()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("StringInterner begin iterator")));
}

TEST_F(StringInternerRequireExceptionsTest, InvalidMapIteratorOperationsThrow) {
  if constexpr (!config::kRequireThrows) {
    GTEST_SKIP() << "requires --//mbo/config:require_throws=true";
  }
  StringInternerMap<int> map;
  const StringInternerMap<int>::iterator singular;
  EXPECT_THAT(
      [&singular] { static_cast<void>(*singular); },
      ThrowsMessage<std::runtime_error>(HasSubstr("singular StringInternerMap iterator")));
  EXPECT_THAT(
      [&map] { static_cast<void>(++map.end()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("StringInternerMap end iterator")));
  EXPECT_THAT(
      [&map] { static_cast<void>(--map.begin()); },
      ThrowsMessage<std::runtime_error>(HasSubstr("StringInternerMap begin iterator")));
}

}  // namespace
}  // namespace mbo::strings
