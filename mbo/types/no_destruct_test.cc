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

#include "mbo/types/no_destruct.h"

#include <string>

#include "absl/hash/hash.h"
#include "absl/log/absl_check.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/types/extend.h"
#include "mbo/types/extender.h"
#include "mbo/types/traits.h"

namespace mbo::types {

using ::mbo::types::Extend;
using ::testing::Ge;  // NOLINT(misc-unused-using-decls)
using ::testing::Ne;

namespace {
constexpr int kValueA = 25;
constexpr int kValueB = 42;

struct TestSimple : Extend<TestSimple> {
  int a = kValueA;
  int b = kValueB;
};

struct TestString : Extend<TestString> {
  std::string a = "25";
  std::string b = "42";
};

struct CountsDestruction {
  explicit CountsDestruction(int& new_count) noexcept : count(&new_count) {}

  ~CountsDestruction() { ++*count; }

  CountsDestruction(const CountsDestruction&) = delete;
  CountsDestruction& operator=(const CountsDestruction&) = delete;
  CountsDestruction(CountsDestruction&&) = delete;
  CountsDestruction& operator=(CountsDestruction&&) = delete;

  int* count;
};

static_assert(IsAggregate<TestSimple>, "Must be aggregate");
static_assert(IsAggregate<TestString>, "Must be aggregate");

static_assert(DecomposeCountV<TestSimple> == 2, "There are 2 fields, no?");
static_assert(DecomposeCountV<TestString> == 2, "There are 2 fields, no?");

const NoDestruct<TestSimple> kTestSimple;
const NoDestruct<TestString> kTestString;

class NoDestructTest : public ::testing::Test {};

#ifdef _LIBCPP_STD_VER
TEST_F(NoDestructTest, ClangCheck) {
  const std::string k_lib_cpp_std_ver = std::to_string(_LIBCPP_STD_VER);
  EXPECT_THAT(k_lib_cpp_std_ver, Ge("20"));
}
#endif  // _LIBCPP_STD_VER

TEST_F(NoDestructTest, Test) {
  constexpr std::string_view kExpectedSimple = "{.a: 25, .b: 42}";
  constexpr std::string_view kExpectedString = R"({.a: "25", .b: "42"})";
  EXPECT_THAT(kTestSimple.Get().ToString(), kExpectedSimple);
  EXPECT_THAT((*kTestSimple).ToString(), kExpectedSimple);
  EXPECT_THAT(kTestSimple->ToString(), kExpectedSimple);
  EXPECT_THAT(kTestString.Get().ToString(), kExpectedString);
  EXPECT_THAT((*kTestString).ToString(), kExpectedString);
  EXPECT_THAT(kTestString->ToString(), kExpectedString);
}

static constexpr NoDestruct<TestSimple> kConstexprTest;  // NOLINT(readability-static-definition-in-anonymous-namespace)

static_assert(sizeof(NoDestruct<int>) == sizeof(int));

TEST_F(NoDestructTest, ConstExpr) {
  EXPECT_THAT(kConstexprTest->a, kValueA);
  EXPECT_THAT(kConstexprTest->b, kValueB);
}

TEST_F(NoDestructTest, Modify) {
  static NoDestruct<TestSimple> test;
  EXPECT_THAT(test->a, kValueA);
  EXPECT_THAT(test->b, kValueB);
  test->a = 3;
  ASSERT_THAT(kValueA, Ne(3));
  EXPECT_THAT(test->a, Ne(kValueA));
  EXPECT_THAT(test->b, kValueB);
  test->a = kValueA;
  EXPECT_THAT(test->a, kValueA);
  EXPECT_THAT(test->b, kValueB);
}

TEST_F(NoDestructTest, HashesAndStringifiesStoredValue) {
  constexpr int kValue = 123;
  const NoDestruct<int> value(kValue);

  EXPECT_THAT(absl::HashOf(value), absl::HashOf(*value));
  EXPECT_THAT(absl::StrCat(value), "123");
}

TEST_F(NoDestructTest, DoesNotDestroyStoredValue) {
  int destruction_count = 0;
  {
    const NoDestruct<CountsDestruction> value(destruction_count);
    EXPECT_THAT(value->count, &destruction_count);
  }

  EXPECT_THAT(destruction_count, 0);
}

TEST_F(NoDestructTest, NoDtorNoCopyNoMove) {
  struct NoDtor {
    NoDtor() = default;

    ~NoDtor() { ABSL_CHECK(true) << "Should not call: " << __FUNCTION__; }

    NoDtor(const NoDtor& other) : a(other.a) { ABSL_CHECK(true) << "Should not call: " << __FUNCTION__; }

    NoDtor(NoDtor&& other) noexcept : a(other.a) { ABSL_CHECK(true) << "Should not call: " << __FUNCTION__; }

    NoDtor& operator=(const NoDtor& other) noexcept {
      ABSL_CHECK(true) << "Should not call: " << __FUNCTION__;
      if (this != &other) {
        a = other.a;
      }
      return *this;
    }

    NoDtor& operator=(NoDtor&& other) noexcept {
      ABSL_CHECK(true) << "Should not call: " << __FUNCTION__;
      a = other.a;
      return *this;
    }

    int a = kValueA;
  };

  NoDestruct<NoDtor> test;
  EXPECT_THAT(test->a, kValueA);
  test->a = kValueB;
  EXPECT_THAT(test->a, kValueB);
  test->a = kValueA;
  EXPECT_THAT(test->a, kValueA);
}

}  // namespace
}  // namespace mbo::types
