// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <stdexcept>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/config/config.h"
#include "mbo/strings/string_interner.h"

namespace mbo::strings {
namespace {

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;

struct StringInternerRequireExceptionsTest : ::testing::Test {};

static_assert(noexcept(*std::declval<StringInterner<>::iterator&>()) == !::mbo::config::kRequireThrows);
static_assert(noexcept(++std::declval<StringInterner<>::iterator&>()) == !::mbo::config::kRequireThrows);
static_assert(noexcept(--std::declval<StringInterner<>::iterator&>()) == !::mbo::config::kRequireThrows);

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

}  // namespace
}  // namespace mbo::strings
