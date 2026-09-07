// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mbo/log/scoped_log_check.h"

#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace mbo::log {

using ::testing::IsEmpty;

namespace {

struct ScopedLogCheckTest : ::testing::Test {};

TEST_F(ScopedLogCheckTest, ScopedLogCheck) {
  {
    testing::internal::CaptureStderr();
    { ScopedLogCheck(true, "Test") << "NothingToShow"; }
    EXPECT_THAT(testing::internal::GetCapturedStderr(), IsEmpty());
  }
  {
    EXPECT_DEATH(
        ScopedLogCheck(false, "BadCheck"),  //
        R"rx(^\[[^\]]*/scoped_log_check_test.cc:[0-9]+\] @.*void mbo::log.*::TestBody\(\) : BadCheck\n$)rx");
  }
  {
    EXPECT_DEATH(
        ScopedLogCheck(false, "BadCheck") << "Here" << "We" << "Go",  //
        R"rx(^\[[^\]]*/scoped_log_check_test.cc:[0-9]+\] @.*void mbo::log.*::TestBody\(\) : BadCheck : HereWeGo\n$)rx");
  }
}

TEST_F(ScopedLogCheckTest, AcceptsStringViewsOnRvalueAndLvalueStreams) {
  testing::internal::CaptureStderr();
  ScopedLogCheck(true, "Test") << std::string_view{"ignored rvalue"};
  auto stream = ScopedLogCheck(true, "Test");
  stream << std::string_view{"ignored lvalue"};
  EXPECT_THAT(testing::internal::GetCapturedStderr(), IsEmpty());
}

TEST_F(ScopedLogCheckTest, MboLogCheck) {
  constexpr int one = 1;  // NOLINT(*-identifier-naming)
  constexpr int two = 2;  // NOLINT(*-identifier-naming)
  {
    testing::internal::CaptureStderr();
    { MBO_LOG_CHECK(one < two) << "NothingToShow"; }
    EXPECT_THAT(testing::internal::GetCapturedStderr(), IsEmpty());
  }
  {
    EXPECT_DEATH(
        ScopedLogCheck(two < one, "BadCheck"),  //
        R"rx(^\[[^\]]*/scoped_log_check_test.cc:[0-9]+\] @.*void mbo::log.*::TestBody\(\) : BadCheck\n$)rx");
  }
  {
    EXPECT_DEATH(
        ScopedLogCheck(two < one, "BadCheck") << "Here" << "We" << "Go",  //
        R"rx(^\[[^\]]*/scoped_log_check_test.cc:[0-9]+\] @.*void mbo::log.*::TestBody\(\) : BadCheck : HereWeGo\n$)rx");
  }
}

}  // namespace
}  // namespace mbo::log
