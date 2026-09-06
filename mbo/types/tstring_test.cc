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

#include "mbo/types/tstring.h"

#include <cstddef>
#include <functional>
#include <iterator>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "absl/hash/hash.h"
#include "absl/hash/hash_testing.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/hash/hash.h"

namespace mbo::types {
namespace {

// NOLINTBEGIN(*-avoid-c-arrays)
// NOLINTBEGIN(*-magic-numbers)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTBEGIN(hicpp-no-array-decay)
// NOLINTBEGIN(readability-static-accessed-through-instance)
// NOLINTBEGIN(readability-static-definition-in-anonymous-namespace)

using ::testing::Conditional;
using ::testing::IsEmpty;
using ::testing::Ne;
using ::testing::Not;
using ::testing::SizeIs;
using ::testing::StrEq;

static_assert(sizeof(""_ts) == 1);
static_assert(""_ts.empty());
static_assert(""_ts.size() == 0);

static_assert(sizeof("test"_ts) == 1);
static_assert(!"test"_ts.empty());
static_assert("test"_ts.size() == 4);

static_assert(sizeof("test-more"_ts) == 1);
static_assert(!"test-more"_ts.empty());
static_assert("test-more"_ts.size() == 9);

// Concat
static_assert(("test"_ts + "-"_ts + "more"_ts).size() == 9);
static_assert(("test"_ts + "-"_ts + "more"_ts) == "test-more");
static_assert(("test"_ts + "-"_ts + "more"_ts) == "test-more"_ts);

// Iteration requirement
static_assert(std::forward_iterator<decltype(""_ts)::iterator>);
static_assert(std::forward_iterator<decltype(""_ts)::const_iterator>);

struct TStringTest : ::testing::Test {
  static constexpr auto kTestA1 = MBO_MAKE_TSTRING("1_test");
  static constexpr std::string_view kTestB1{MBO_MAKE_TSTRING("1_test")};
  static const std::string kTestC1;
  static constexpr auto kTestA2 = MBO_MAKE_TSTRING("2_test");
  static constexpr std::string_view kTestB2{MBO_MAKE_TSTRING("2_test")};
  static const std::string kTestC2;
};

const std::string TStringTest::kTestC1{"1_test"};
const std::string TStringTest::kTestC2{"2_test"};

template<tstring TString, std::size_t Length>
struct TestInfo {
  static constexpr tstring kStr = TString;
  static constexpr std::size_t kLength = Length;
};

template<typename T>
struct GenTStringTest : ::testing::Test {};

TEST_F(TStringTest, Test) {
  EXPECT_THAT(std::string_view("test"_ts), StrEq("test"));
  EXPECT_THAT((""_ts).empty(), true);
  EXPECT_THAT((""_ts).size(), 0);
  EXPECT_THAT(("\0"_ts).empty(), false);
  EXPECT_THAT(("\0"_ts).size(), 1);
  EXPECT_THAT(("1\0\n4"_ts).empty(), false);
  EXPECT_THAT(("1\0\n4"_ts).size(), 4);
  EXPECT_THAT(("12"_ts).empty(), false);
  EXPECT_THAT(("12"_ts).size(), 2);
  using MyTstring = tstring<'m', 'y', 'm', 'y'>;
  EXPECT_THAT(MyTstring::size(), 4);
  const MyTstring mymy;
  EXPECT_THAT(decltype(mymy)::size(), 4);
}

TEST_F(TStringTest, Types) {
  {
    static constexpr tstring kVarA = "1_test"_ts;
    static constexpr tstring kVarB = "1_test"_ts;
    static constexpr tstring<'1', '_', 't', 'e', 's', 't'> kVarC;
    static constexpr tstring kVarD = "2_test"_ts;
    static constexpr tstring<'2', '_', 't', 'e', 's', 't'> kVvarE;
    EXPECT_THAT((std::is_same_v<decltype(kVarA), decltype(kVarB)>), true);
    EXPECT_THAT((std::is_same_v<decltype(kVarA), decltype(kVarC)>), true);
    EXPECT_THAT((std::is_same_v<decltype(kVarA), decltype(kVarD)>), false);
    EXPECT_THAT((std::is_same_v<decltype(kVarA), decltype(kVvarE)>), false);
    EXPECT_THAT((std::is_same_v<decltype(kVarB), decltype(kVarC)>), true);
    EXPECT_THAT((std::is_same_v<decltype(kVarB), decltype(kVarD)>), false);
    EXPECT_THAT((std::is_same_v<decltype(kVarB), decltype(kVvarE)>), false);
    EXPECT_THAT((std::is_same_v<decltype(kVarC), decltype(kVarD)>), false);
    EXPECT_THAT((std::is_same_v<decltype(kVarC), decltype(kVvarE)>), false);
    EXPECT_THAT((std::is_same_v<decltype(kVarD), decltype(kVvarE)>), true);
  }
  {
    static constexpr tstring<'1'> kVarA;
    static constexpr tstring<'\0'> kVarB;
    static constexpr tstring<'1', '\0'> kVarC;
    static constexpr tstring<> kVarD;
    EXPECT_THAT((std::is_same_v<decltype(kVarA), decltype(kVarB)>), false);
    EXPECT_THAT((std::is_same_v<decltype(kVarA), decltype(kVarC)>), false);
    EXPECT_THAT((std::is_same_v<decltype(kVarA), decltype(kVarD)>), false);
    EXPECT_THAT((std::is_same_v<decltype(kVarB), decltype(kVarC)>), false);
    EXPECT_THAT((std::is_same_v<decltype(kVarB), decltype(kVarD)>), false);
    EXPECT_THAT((std::is_same_v<decltype(kVarC), decltype(kVarD)>), false);
  }
}

TEST_F(TStringTest, MacroMakeTString) {
  static constexpr auto kVarA = MBO_MAKE_TSTRING("1_test");
  static constexpr auto kVarB = MBO_MAKE_TSTRING("1_test");
  static constexpr auto kVarC = MBO_MAKE_TSTRING("1_test");
  static constexpr auto kVarD = MBO_MAKE_TSTRING("2_test");
  EXPECT_THAT((std::is_same_v<decltype(kVarA), decltype(kVarB)>), true);
  EXPECT_THAT((std::is_same_v<decltype(kVarA), decltype(kVarC)>), true);
  EXPECT_THAT((std::is_same_v<decltype(kVarA), decltype(kVarD)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kVarB), decltype(kVarC)>), true);
  EXPECT_THAT((std::is_same_v<decltype(kVarB), decltype(kVarD)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kVarC), decltype(kVarD)>), false);
}

TEST_F(TStringTest, TypeEq) {
  EXPECT_THAT((std::is_same_v<decltype(kTestA1), decltype(kTestA2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTestA1), decltype(kTestB1)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTestA1), decltype(kTestB2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTestA1), decltype(kTestC1)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTestA1), decltype(kTestC2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTestA2), decltype(kTestB1)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTestA2), decltype(kTestB2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTestA2), decltype(kTestC1)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTestA2), decltype(kTestC2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTestB1), decltype(kTestB2)>), true);
  EXPECT_THAT((std::is_same_v<decltype(kTestB1), decltype(kTestC2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTestB1), decltype(kTestC2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTestC1), decltype(kTestC2)>), true);
}

TEST_F(TStringTest, Is) {
  static constexpr auto kTtestD1 = MBO_MAKE_TSTRING("1_test");
  static constexpr const auto& kTestE1 = kTestA1;
  static constexpr const auto* kTestF1 = &kTestA1;

  EXPECT_THAT(kTestA1.is("1_test"_ts), true);
  EXPECT_THAT(kTestA1.is("2_test"_ts), false);
  EXPECT_THAT(kTestA1.is(kTestA2), false);
  EXPECT_THAT(kTestA1.is(kTestB1), false);
  EXPECT_THAT(kTestA1.is(kTestC1), false);
  EXPECT_THAT(kTestA1.is(kTtestD1), true);
  EXPECT_THAT(kTestA1.is(kTestE1), true);
  EXPECT_THAT(kTestA1.is(kTestF1), false);
  EXPECT_THAT(kTestA1.is(*kTestF1), true);
}

TEST_F(TStringTest, Compare) {
  EXPECT_THAT(kTestA1.compare("1_test"), 0);
  EXPECT_THAT(kTestA1.compare("2_test"), -1);
  EXPECT_THAT(kTestA2.compare("1_test"), 1);
}

TEST_F(TStringTest, Eq) {
  EXPECT_THAT(kTestA1 == "1_test", true);
  EXPECT_THAT(kTestA1 == "2_test", false);
  EXPECT_THAT(kTestA2 == "1_test", false);

  EXPECT_THAT(kTestA1 == kTestA1, true);
  EXPECT_THAT(kTestA1 == kTestA2, false);
  EXPECT_THAT(kTestA1 == kTestB1, true);
  EXPECT_THAT(kTestA1 == kTestB2, false);
  EXPECT_THAT(kTestA1 == kTestC1, true);
  EXPECT_THAT(kTestA1 == kTestC2, false);
  EXPECT_THAT(kTestA2 == kTestA1, false);
  EXPECT_THAT(kTestB1 == kTestA1, true);
  EXPECT_THAT(kTestB2 == kTestA1, false);
  EXPECT_THAT(kTestC1 == kTestA1, true);
  EXPECT_THAT(kTestC2 == kTestA1, false);
}

TEST_F(TStringTest, Ne) {
  EXPECT_THAT(kTestA1 != "1_test", false);
  EXPECT_THAT(kTestA1 != "2_test", true);
  EXPECT_THAT(kTestA2 != "1_test", true);

  EXPECT_THAT(kTestA1 != kTestA1, false);
  EXPECT_THAT(kTestA1 != kTestA2, true);
  EXPECT_THAT(kTestA1 != kTestB1, false);
  EXPECT_THAT(kTestA1 != kTestB2, true);
  EXPECT_THAT(kTestA1 != kTestC1, false);
  EXPECT_THAT(kTestA1 != kTestC2, true);
  EXPECT_THAT(kTestA2 != kTestA1, true);
  EXPECT_THAT(kTestB1 != kTestA1, false);
  EXPECT_THAT(kTestB2 != kTestA1, true);
  EXPECT_THAT(kTestC1 != kTestA1, false);
  EXPECT_THAT(kTestC2 != kTestA1, true);
}

TEST_F(TStringTest, Lt) {
  EXPECT_THAT(kTestA1 < "1_test", false);
  EXPECT_THAT(kTestA1 < "2_test", true);
  EXPECT_THAT(kTestA2 < "1_test", false);

  EXPECT_THAT(kTestA1 < kTestA1, false);
  EXPECT_THAT(kTestA1 < kTestA2, true);
  EXPECT_THAT(kTestA1 < kTestB1, false);
  EXPECT_THAT(kTestA1 < kTestB2, true);
  EXPECT_THAT(kTestA1 < kTestC1, false);
  EXPECT_THAT(kTestA1 < kTestC2, true);

  EXPECT_THAT(kTestA2 < kTestA1, false);
  EXPECT_THAT(kTestA2 < kTestA2, false);
  EXPECT_THAT(kTestA2 < kTestB1, false);
  EXPECT_THAT(kTestA2 < kTestB2, false);
  EXPECT_THAT(kTestA2 < kTestC1, false);
  EXPECT_THAT(kTestA2 < kTestC2, false);

  EXPECT_THAT(kTestB1 < kTestA1, false);
  EXPECT_THAT(kTestB2 < kTestA1, false);
  EXPECT_THAT(kTestC1 < kTestA1, false);
  EXPECT_THAT(kTestC2 < kTestA1, false);

  EXPECT_THAT(kTestB1 < kTestA2, true);
  EXPECT_THAT(kTestB2 < kTestA2, false);
  EXPECT_THAT(kTestC1 < kTestA2, true);
  EXPECT_THAT(kTestC2 < kTestA2, false);
}

TEST_F(TStringTest, Le) {
  EXPECT_THAT(kTestA1 <= "1_test", true);
  EXPECT_THAT(kTestA1 <= "2_test", true);
  EXPECT_THAT(kTestA2 <= "1_test", false);

  EXPECT_THAT(kTestA1 <= kTestA1, true);
  EXPECT_THAT(kTestA1 <= kTestA2, true);
  EXPECT_THAT(kTestA1 <= kTestB1, true);
  EXPECT_THAT(kTestA1 <= kTestB2, true);
  EXPECT_THAT(kTestA1 <= kTestC1, true);
  EXPECT_THAT(kTestA1 <= kTestC2, true);

  EXPECT_THAT(kTestA2 <= kTestA1, false);
  EXPECT_THAT(kTestA2 <= kTestA2, true);
  EXPECT_THAT(kTestA2 <= kTestB1, false);
  EXPECT_THAT(kTestA2 <= kTestB2, true);
  EXPECT_THAT(kTestA2 <= kTestC1, false);
  EXPECT_THAT(kTestA2 <= kTestC2, true);

  EXPECT_THAT(kTestB1 <= kTestA1, true);
  EXPECT_THAT(kTestB2 <= kTestA1, false);
  EXPECT_THAT(kTestC1 <= kTestA1, true);
  EXPECT_THAT(kTestC2 <= kTestA1, false);

  EXPECT_THAT(kTestB1 <= kTestA2, true);
  EXPECT_THAT(kTestB2 <= kTestA2, true);
  EXPECT_THAT(kTestC1 <= kTestA2, true);
  EXPECT_THAT(kTestC2 <= kTestA2, true);
}

TEST_F(TStringTest, Gt) {
  EXPECT_THAT(kTestA1 > "1_test", false);
  EXPECT_THAT(kTestA1 > "2_test", false);
  EXPECT_THAT(kTestA2 > "1_test", true);

  EXPECT_THAT(kTestA1 > kTestA1, false);
  EXPECT_THAT(kTestA1 > kTestA2, false);
  EXPECT_THAT(kTestA1 > kTestB1, false);
  EXPECT_THAT(kTestA1 > kTestB2, false);
  EXPECT_THAT(kTestA1 > kTestC1, false);
  EXPECT_THAT(kTestA1 > kTestC2, false);

  EXPECT_THAT(kTestA2 > kTestA1, true);
  EXPECT_THAT(kTestA2 > kTestA2, false);
  EXPECT_THAT(kTestA2 > kTestB1, true);
  EXPECT_THAT(kTestA2 > kTestB2, false);
  EXPECT_THAT(kTestA2 > kTestC1, true);
  EXPECT_THAT(kTestA2 > kTestC2, false);

  EXPECT_THAT(kTestB1 > kTestA1, false);
  EXPECT_THAT(kTestB2 > kTestA1, true);
  EXPECT_THAT(kTestC1 > kTestA1, false);
  EXPECT_THAT(kTestC2 > kTestA1, true);

  EXPECT_THAT(kTestB1 > kTestA2, false);
  EXPECT_THAT(kTestB2 > kTestA2, false);
  EXPECT_THAT(kTestC1 > kTestA2, false);
  EXPECT_THAT(kTestC2 > kTestA2, false);
}

TEST_F(TStringTest, Ge) {
  EXPECT_THAT(kTestA1 >= "1_test", true);
  EXPECT_THAT(kTestA1 >= "2_test", false);
  EXPECT_THAT(kTestA2 >= "1_test", true);

  EXPECT_THAT(kTestA1 >= kTestA1, true);
  EXPECT_THAT(kTestA1 >= kTestA2, false);
  EXPECT_THAT(kTestA1 >= kTestB1, true);
  EXPECT_THAT(kTestA1 >= kTestB2, false);
  EXPECT_THAT(kTestA1 >= kTestC1, true);
  EXPECT_THAT(kTestA1 >= kTestC2, false);

  EXPECT_THAT(kTestA2 >= kTestA1, true);
  EXPECT_THAT(kTestA2 >= kTestA2, true);
  EXPECT_THAT(kTestA2 >= kTestB1, true);
  EXPECT_THAT(kTestA2 >= kTestB2, true);
  EXPECT_THAT(kTestA2 >= kTestC1, true);
  EXPECT_THAT(kTestA2 >= kTestC2, true);

  EXPECT_THAT(kTestB1 >= kTestA1, true);
  EXPECT_THAT(kTestB2 >= kTestA1, true);
  EXPECT_THAT(kTestC1 >= kTestA1, true);
  EXPECT_THAT(kTestC2 >= kTestA1, true);

  EXPECT_THAT(kTestB1 >= kTestA2, false);
  EXPECT_THAT(kTestB2 >= kTestA2, true);
  EXPECT_THAT(kTestC1 >= kTestA2, false);
  EXPECT_THAT(kTestC2 >= kTestA2, true);
}

TEST_F(TStringTest, Concat) {
  EXPECT_THAT(tstring<>() + tstring<>(), tstring<>());
  EXPECT_THAT((tstring<'a'>() + tstring<'.'>() + tstring<'b'>()), (tstring<'a', '.', 'b'>()));
  EXPECT_THAT("a"_ts + "."_ts + "b"_ts, "a.b"_ts);
  EXPECT_THAT("a"_ts + ""_ts + "."_ts + ""_ts + "b"_ts + ""_ts, "a.b"_ts);
}

TEST_F(TStringTest, StreamsItsStringValue) {
  std::ostringstream out;
  out << "streamed"_ts;
  EXPECT_THAT(out.str(), "streamed");
}

static constexpr const char kGsv0[]{""};
static constexpr const char kGsv1[]{"g"};
static constexpr const char kGsv2[]{"gs"};
static constexpr const char kGsv3[]{"gsv"};
static constexpr const char kGsv4[]{"gsv4"};
static constexpr const char kGsv5[]{"gsv_5"};
static constexpr const char kGsv6[7]{"gsv_6\0"};  // Tests '\0' char handling.

TEST_F(TStringTest, MakeTString) {
  using types_internal::MakeTstringHelper;
  static constexpr const char kTestA0[]{""};
  static constexpr const char kTestB0[]{""};
  static constexpr const char kTtestA1[]{"a"};
  static constexpr const char kTtestB1[]{"b"};
  static constexpr const char kTestA2[]{"a2"};
  static constexpr const char kTestB2[]{"b2"};
  static constexpr const char kTtestC2[]{"a2"};

  constexpr auto kThA0 = MakeTstringHelper<kTestA0>::tstr();
  constexpr auto kTsA0 = make_tstring<kTestA0>();
  EXPECT_THAT(kThA0.size(), 0);
  EXPECT_THAT(kThA0.str(), StrEq(""));

  constexpr auto kThB0 = MakeTstringHelper<kTestB0>::tstr();
  constexpr auto kTsB0 = make_tstring<kTestB0>();
  EXPECT_THAT(kThB0.size(), 0);
  EXPECT_THAT(kThB0.str(), StrEq(""));
  EXPECT_THAT((std::is_same_v<decltype(kTsA0), decltype(kTsB0)>), true);

  constexpr auto kThA1 = MakeTstringHelper<kTtestA1>::tstr();
  constexpr auto kTsA1 = make_tstring<kTtestA1>();
  EXPECT_THAT(kThA1.size(), 1);
  EXPECT_THAT(kThA1.str(), StrEq("a"));
  EXPECT_THAT(kTsA1.size(), 1);
  EXPECT_THAT(kTsA1.str(), StrEq("a"));
  EXPECT_THAT((std::is_same_v<decltype(kTsA0), decltype(kTsA1)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsB0), decltype(kTsA1)>), false);

  constexpr auto kThB1 = MakeTstringHelper<kTtestB1>::tstr();
  constexpr auto kTsB1 = make_tstring<kTtestB1>();
  EXPECT_THAT(kThB1.size(), 1);
  EXPECT_THAT(kThB1.str(), StrEq("b"));
  EXPECT_THAT(kTsB1.size(), 1);
  EXPECT_THAT(kTsB1.str(), StrEq("b"));
  EXPECT_THAT((std::is_same_v<decltype(kTsA0), decltype(kTsB1)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsB0), decltype(kTsB1)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsA1), decltype(kTsB1)>), false);

  constexpr auto kThA2 = MakeTstringHelper<kTestA2>::tstr();
  constexpr auto kTsA2 = make_tstring<kTestA2>();
  EXPECT_THAT(kThA2.size(), 2);
  EXPECT_THAT(kThA2.str(), StrEq("a2"));
  EXPECT_THAT(kTsA2.size(), 2);
  EXPECT_THAT(kTsA2.str(), StrEq("a2"));
  EXPECT_THAT((std::is_same_v<decltype(kTsA0), decltype(kTsA2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsB0), decltype(kTsA2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsA1), decltype(kTsA2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsB1), decltype(kTsA2)>), false);

  constexpr auto kThB2 = MakeTstringHelper<kTestB2>::tstr();
  constexpr auto kTsB2 = make_tstring<kTestB2>();
  EXPECT_THAT(kThB2.size(), 2);
  EXPECT_THAT(kThB2.str(), StrEq("b2"));
  EXPECT_THAT(kTsB2.size(), 2);
  EXPECT_THAT(kTsB2.str(), StrEq("b2"));
  EXPECT_THAT((std::is_same_v<decltype(kTsA0), decltype(kTsB2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsB0), decltype(kTsB2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsA1), decltype(kTsB2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsB1), decltype(kTsB2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsA2), decltype(kTsB2)>), false);

  constexpr auto kThC2 = MakeTstringHelper<kTtestC2>::tstr();
  constexpr auto kTsC2 = make_tstring<kTtestC2>();
  EXPECT_THAT(kThC2.size(), 2);
  EXPECT_THAT(kThC2.str(), StrEq("a2"));
  EXPECT_THAT(kTsC2.size(), 2);
  EXPECT_THAT(kTsC2.str(), StrEq("a2"));
  EXPECT_THAT((std::is_same_v<decltype(kTsA0), decltype(kTsC2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsB0), decltype(kTsC2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsA1), decltype(kTsC2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsB1), decltype(kTsC2)>), false);
  EXPECT_THAT((std::is_same_v<decltype(kTsA2), decltype(kTsC2)>), true);
}

using MyTypes = ::testing::Types<
    TestInfo<""_ts, 0>,
    TestInfo<"1"_ts, 1>,
    TestInfo<"12"_ts, 2>,
    TestInfo<"123"_ts, 3>,
    TestInfo<"1234"_ts, 4>,
    TestInfo<"12345"_ts, 5>,
    TestInfo<"12345\0"_ts, 6>,
    TestInfo<MBO_MAKE_TSTRING(""), 0>,
    TestInfo<MBO_MAKE_TSTRING("a"), 1>,
    TestInfo<MBO_MAKE_TSTRING("ab"), 2>,
    TestInfo<MBO_MAKE_TSTRING("abc"), 3>,
    TestInfo<MBO_MAKE_TSTRING("abcd"), 4>,
    TestInfo<MBO_MAKE_TSTRING("abcde"), 5>,
    TestInfo<MBO_MAKE_TSTRING("abcde\0"), 6>,
    TestInfo<make_tstring<kGsv0>(), 0>,
    TestInfo<make_tstring<kGsv1>(), 1>,
    TestInfo<make_tstring<kGsv2>(), 2>,
    TestInfo<make_tstring<kGsv3>(), 3>,
    TestInfo<make_tstring<kGsv4>(), 4>,
    TestInfo<make_tstring<kGsv5>(), 5>,
    TestInfo<make_tstring<kGsv6>(), 5>,  // \0 not considered.
    TestInfo<tstring<>{}, 0>,
    TestInfo<tstring<'A'>{}, 1>,
    TestInfo<tstring<'A', 'B'>{}, 2>,
    TestInfo<tstring<'A', 'B', 'C'>{}, 3>,
    TestInfo<tstring<'A', 'B', 'C', 'D'>{}, 4>,
    TestInfo<tstring<'A', 'B', 'C', 'D', 'E'>{}, 5>,
    TestInfo<tstring<'A', 'B', 'C', 'D', 'E', '\0'>{}, 6>>;
TYPED_TEST_SUITE(GenTStringTest, MyTypes);

TYPED_TEST(GenTStringTest, Test) {
  EXPECT_THAT(TypeParam::kStr, SizeIs(TypeParam::kLength)) << "String = '" << TypeParam::kStr << "'";
  EXPECT_THAT(TypeParam::kStr, Conditional(TypeParam::kLength, Not(IsEmpty()), IsEmpty()));
  EXPECT_THAT(sizeof(TypeParam::kStr), 1) << "Type `tstring` is not supposed to have any non static fields, so the "
                                             "size should be 1";
}

TEST_F(TStringTest, Substr) {
  constexpr auto kTest0to9 = "0123456789"_ts;
  ASSERT_THAT(kTest0to9.length(), 10);
  EXPECT_THAT((kTest0to9.substr<>()), "0123456789"_ts);
  EXPECT_THAT((kTest0to9.substr<0>()), "0123456789"_ts);
  EXPECT_THAT((kTest0to9.substr<0, 10>()), "0123456789"_ts);
  EXPECT_THAT((kTest0to9.substr<0, 20>()), "0123456789"_ts);
  EXPECT_THAT((kTest0to9.substr<0, kTest0to9.npos>()), "0123456789"_ts);

  EXPECT_THAT((kTest0to9.substr<0, 0>()), ""_ts);
  EXPECT_THAT((kTest0to9.substr<0, 2>()), "01"_ts);
  EXPECT_THAT((kTest0to9.substr<3>()), "3456789"_ts);
  EXPECT_THAT((kTest0to9.substr<3, 4>()), "3456"_ts);
  EXPECT_THAT((kTest0to9.substr<8, 0>()), ""_ts);
  EXPECT_THAT((kTest0to9.substr<8, 1>()), "8"_ts);
  EXPECT_THAT((kTest0to9.substr<8, 2>()), "89"_ts);
  EXPECT_THAT((kTest0to9.substr<8, 3>()), "89"_ts);

  EXPECT_THAT((kTest0to9.substr<42>()), ""_ts);
  EXPECT_THAT((kTest0to9.substr<42, 0>()), ""_ts);
  EXPECT_THAT((kTest0to9.substr<42, 1>()), ""_ts);
  EXPECT_THAT((kTest0to9.substr<42, 25>()), ""_ts);
}

TEST_F(TStringTest, RunTimeSubstr) {
  constexpr auto kTest0to9 = "0123456789"_ts;
  ASSERT_THAT(kTest0to9.length(), 10);
  EXPECT_THAT((kTest0to9.substr()), "0123456789"_ts);
  EXPECT_THAT((kTest0to9.substr(0)), "0123456789"_ts);
  EXPECT_THAT((kTest0to9.substr(0, 10)), "0123456789"_ts);
  EXPECT_THAT((kTest0to9.substr(0, 20)), "0123456789"_ts);
  EXPECT_THAT((kTest0to9.substr(0, kTest0to9.npos)), "0123456789"_ts);

  EXPECT_THAT((kTest0to9.substr(0, 0)), ""_ts);
  EXPECT_THAT((kTest0to9.substr(0, 2)), "01"_ts);
  EXPECT_THAT((kTest0to9.substr(3)), "3456789"_ts);
  EXPECT_THAT((kTest0to9.substr(3, 4)), "3456"_ts);
  EXPECT_THAT((kTest0to9.substr(8, 0)), ""_ts);
  EXPECT_THAT((kTest0to9.substr(8, 1)), "8"_ts);
  EXPECT_THAT((kTest0to9.substr(8, 2)), "89"_ts);
  EXPECT_THAT((kTest0to9.substr(8, 3)), "89"_ts);

  EXPECT_THAT((kTest0to9.substr(42)), ""_ts);
  EXPECT_THAT((kTest0to9.substr(42, 0)), ""_ts);
  EXPECT_THAT((kTest0to9.substr(42, 1)), ""_ts);
  EXPECT_THAT((kTest0to9.substr(42, 25)), ""_ts);
}

TEST_F(TStringTest, StartsWith) {
  constexpr auto kTest0to9 = "0123456789"_ts;
  EXPECT_THAT(kTest0to9.starts_with(kTest0to9), true);
  EXPECT_THAT(kTest0to9.starts_with(""_ts), true);
  EXPECT_THAT(kTest0to9.starts_with("0"_ts), true);
  EXPECT_THAT(kTest0to9.starts_with("01"_ts), true);
  EXPECT_THAT(kTest0to9.starts_with("1"_ts), false);
  EXPECT_THAT(kTest0to9.starts_with("012345678901"_ts), false);
  EXPECT_THAT(kTest0to9.starts_with("1123456789"_ts), false);
}

TEST_F(TStringTest, EndswithWith) {
  constexpr auto kTest0to9 = "0123456789"_ts;
  EXPECT_THAT(kTest0to9.ends_with(kTest0to9), true);
  EXPECT_THAT(kTest0to9.ends_with(""_ts), true);
  EXPECT_THAT(kTest0to9.ends_with("9"_ts), true);
  EXPECT_THAT(kTest0to9.ends_with("89"_ts), true);
  EXPECT_THAT(kTest0to9.ends_with("1"_ts), false);
  EXPECT_THAT(kTest0to9.ends_with("00123456789"_ts), false);
  EXPECT_THAT(kTest0to9.ends_with("1123456789"_ts), false);
}

TEST_F(TStringTest, Find) {
  constexpr auto kTest0to9 = "0123456789"_ts;
  EXPECT_THAT(kTest0to9.find(kTest0to9), 0);
  EXPECT_THAT(kTest0to9.find(""_ts), 0);
  EXPECT_THAT(kTest0to9.find("9"_ts), 9);
  EXPECT_THAT(kTest0to9.find("89"_ts), 8);
  EXPECT_THAT(kTest0to9.find("0"_ts), 0);
  EXPECT_THAT(kTest0to9.find("01"_ts), 0);
  EXPECT_THAT(kTest0to9.find("12"_ts), 1);
  EXPECT_THAT(kTest0to9.find("012345678"_ts), 0);
  EXPECT_THAT(kTest0to9.find("123456789"_ts), 1);
  EXPECT_THAT(kTest0to9.find("a"_ts), kTest0to9.npos);
  EXPECT_THAT(kTest0to9.find("42"_ts), kTest0to9.npos);
  EXPECT_THAT(kTest0to9.find("00123456789"_ts), kTest0to9.npos);
  EXPECT_THAT(kTest0to9.find("01234567899"_ts), kTest0to9.npos);
  constexpr auto kTestABC = "abcabcabc"_ts;
  EXPECT_THAT(kTestABC.find("abc"_ts), 0);
  EXPECT_THAT(kTestABC.find("bc"_ts), 1);
  EXPECT_THAT(kTestABC.find("c"_ts), 2);
}

TEST_F(TStringTest, RFind) {
  constexpr auto kTest0to9 = "0123456789"_ts;
  EXPECT_THAT(kTest0to9.rfind(kTest0to9), 0);  //
  EXPECT_THAT(kTest0to9.rfind(""_ts), 10);
  EXPECT_THAT(kTest0to9.rfind("9"_ts), 9);
  EXPECT_THAT(kTest0to9.rfind("89"_ts), 8);
  EXPECT_THAT(kTest0to9.rfind("0"_ts), 0);
  EXPECT_THAT(kTest0to9.rfind("01"_ts), 0);  //
  EXPECT_THAT(kTest0to9.rfind("12"_ts), 1);
  EXPECT_THAT(kTest0to9.rfind("345"_ts), 3);
  EXPECT_THAT(kTest0to9.rfind("678"_ts), 6);
  EXPECT_THAT(kTest0to9.rfind("012345678"_ts), 0);  //
  EXPECT_THAT(kTest0to9.rfind("12345678"_ts), 1);   //
  EXPECT_THAT(kTest0to9.rfind("23456789"_ts), 2);   //
  EXPECT_THAT(kTest0to9.rfind("123456789"_ts), 1);  //
  EXPECT_THAT(kTest0to9.rfind("a"_ts), kTest0to9.npos);
  EXPECT_THAT(kTest0to9.rfind("42"_ts), kTest0to9.npos);
  EXPECT_THAT(kTest0to9.rfind("00123456789"_ts), kTest0to9.npos);
  EXPECT_THAT(kTest0to9.rfind("01234567899"_ts), kTest0to9.npos);
  constexpr auto kTestABC = "abcabcabc"_ts;
  EXPECT_THAT(kTestABC.rfind("abc"_ts), 6);
  EXPECT_THAT(kTestABC.rfind("bc"_ts), 7);
  EXPECT_THAT(kTestABC.rfind("c"_ts), 8);
}

TEST_F(TStringTest, Contains) {
  constexpr auto kTest0to9 = "0123456789"_ts;
  EXPECT_THAT(kTest0to9.contains(kTest0to9), true);
  EXPECT_THAT(kTest0to9.contains(""_ts), true);
  EXPECT_THAT(kTest0to9.contains("9"_ts), true);
  EXPECT_THAT(kTest0to9.contains("89"_ts), true);
  EXPECT_THAT(kTest0to9.contains("0"_ts), true);
  EXPECT_THAT(kTest0to9.contains("01"_ts), true);
  EXPECT_THAT(kTest0to9.contains("012345678"_ts), true);
  EXPECT_THAT(kTest0to9.contains("123456789"_ts), true);
  EXPECT_THAT(kTest0to9.contains("a"_ts), false);
  EXPECT_THAT(kTest0to9.contains("42"_ts), false);
  EXPECT_THAT(kTest0to9.contains("00123456789"_ts), false);
  EXPECT_THAT(kTest0to9.contains("01234567899"_ts), false);
}

TEST_F(TStringTest, BeginEnd) {
  {
    std::string str;
    for (const char chr : kTestA1) {
      str += chr;
    }
    EXPECT_THAT(str, kTestA1.str());
  }
  {
    std::string rev;
    for (const char it : std::ranges::reverse_view(kTestA1)) {
      rev += it;
    }
    EXPECT_THAT(rev, "tset_1");
  }
}

TEST_F(TStringTest, FindFirstLast) {
  {
    static constexpr std::size_t kPosF = kTestA1.find_first_of('e');  // Verify `constexpr`
    EXPECT_THAT(kPosF, 3);
  }
  {
    static constexpr std::size_t kPos = kTestA1.find_first_of("asx");  // Verify `constexpr`
    EXPECT_THAT(kPos, 4);
    static constexpr std::size_t kPos2 = kTestA1.find_first_of("astx");  // Verify `constexpr`
    EXPECT_THAT(kPos2, 2);
    static constexpr std::size_t kPos3 = kTestA1.find_first_of("astx", 3);  // Verify `constexpr`
    EXPECT_THAT(kPos3, 4);
  }
  const std::string runtime_first_chars = "astx";
  EXPECT_THAT(kTestA1.find_first_of(std::string_view(runtime_first_chars), 3), 4);
  const std::string runtime_missing_chars = "qz";
  EXPECT_THAT(kTestA1.find_first_of(std::string_view(runtime_missing_chars)), tstring<>::npos);
  EXPECT_THAT(kTestA1.find_first_of('e', 3), 3);
  EXPECT_THAT(kTestA1.find_first_of('e', 4), tstring<>::npos);
  EXPECT_THAT(kTestA1.find_first_of('t'), 2);
  EXPECT_THAT(kTestA1.find_first_of('t', 2), 2);
  EXPECT_THAT(kTestA1.find_first_of('t', 3), 5);
  EXPECT_THAT(kTestA1.find_first_of('x'), tstring<>::npos);
  {
    static constexpr std::size_t kPos = kTestA1.find_last_of('e');  // Verify `constexpr`
    EXPECT_THAT(kPos, 3);
    static constexpr std::size_t kPos2 = kTestA1.find_last_of('t');  // Verify `constexpr`
    EXPECT_THAT(kPos2, 5);
  }
  {
    static constexpr std::size_t kPos = kTestA1.find_last_of("asx");  // Verify `constexpr`
    EXPECT_THAT(kPos, 4);
    static constexpr std::size_t kPos2 = kTestA1.find_last_of("astx");  // Verify `constexpr`
    EXPECT_THAT(kPos2, 5);
    static constexpr std::size_t kPos3 = kTestA1.find_last_of("atx", 4);  // Verify `constexpr`
    EXPECT_THAT(kPos3, 2);
  }
  const std::string runtime_last_chars = "atx";
  EXPECT_THAT(kTestA1.find_last_of(std::string_view(runtime_last_chars)), 5);
  EXPECT_THAT(kTestA1.find_last_of(std::string_view(runtime_last_chars), 4), 2);
  EXPECT_THAT(kTestA1.find_last_of(std::string_view(runtime_missing_chars)), tstring<>::npos);
  EXPECT_THAT(kTestA1.find_last_of('t'), 5);  // not 3 which would be first
  EXPECT_THAT(kTestA1.find_last_of('t', 5), 5);
  EXPECT_THAT(kTestA1.find_last_of('t', 4), 2);
  EXPECT_THAT(kTestA1.find_last_of('x'), tstring<>::npos);
}

TEST_F(TStringTest, StdHash) {
  using ::mbo::hash::GetHash64;
  // "bar"_ts
  constexpr tstring<'b', 'a', 'r'> kTsBar;
  using TsBar = decltype(kTsBar);
  const std::size_t k_bar_hash_ts = std::hash<TsBar>{}(kTsBar);
  EXPECT_THAT(std::hash<TsBar>{}(), k_bar_hash_ts);
  constexpr auto kBarDirectHashTs = GetHash64("bar");
  EXPECT_THAT(kBarDirectHashTs, Ne(k_bar_hash_ts));
  EXPECT_THAT(kBarDirectHashTs, decltype(kTsBar)::StringHash());
  const std::string_view vs_bar{"bar"};
  const auto k_bar_volatile_hash_ts = GetHash64(vs_bar);
  EXPECT_THAT(k_bar_volatile_hash_ts, kBarDirectHashTs);
  // "foo"_ts
  constexpr tstring<'f', 'o', 'o'> kTsFoo;
  using TsFoo = decltype(kTsFoo);
  const std::size_t k_foo_hash_ts = std::hash<TsFoo>{}(kTsFoo);
  EXPECT_THAT(std::hash<TsFoo>{}(), k_foo_hash_ts);
  constexpr auto kFooDirectHashTs = GetHash64("foo");
  EXPECT_THAT(kFooDirectHashTs, Ne(k_foo_hash_ts));
  EXPECT_THAT(kFooDirectHashTs, decltype(kTsFoo)::StringHash());
  const std::string_view vs_foo{"foo"};
  const auto k_foo_volatile_hash_ts = GetHash64(vs_foo);
  EXPECT_THAT(k_foo_volatile_hash_ts, kFooDirectHashTs);
  // hash("bar"_ts) != hash("foo"_ts)
  EXPECT_THAT(k_bar_hash_ts, Ne(k_foo_hash_ts));
  // Should **not** match std::hash of std::string and std::string_view.
  const std::size_t k_foo_hash_string = std::hash<std::string>{}(std::string("foo"));
  const std::size_t k_foo_hash_view = std::hash<std::string_view>{}(std::string_view("foo"));
  EXPECT_THAT(k_foo_hash_string, k_foo_hash_view);
  EXPECT_THAT(k_foo_hash_string, Ne(k_foo_hash_ts));
  EXPECT_THAT(k_foo_hash_view, Ne(k_foo_hash_ts));
  // Should **not** match std::hash of const char*.
  const std::size_t k_foo_hash_char_ptr = std::hash<const char*>{}("foo");
  EXPECT_THAT(k_foo_hash_char_ptr, Ne(k_foo_hash_ts));
  EXPECT_THAT(k_foo_hash_string, Ne(k_foo_hash_char_ptr));
  EXPECT_THAT(k_foo_hash_view, Ne(k_foo_hash_char_ptr));
}

TEST_F(TStringTest, AbseilHash) {
  constexpr tstring<'b', 'a', 'r'> kTsBar;
  constexpr tstring<'b', 'a', 'z'> kTsBaz;
  constexpr tstring<'f', 'o', 'o'> kTsFoo;
  // This cannot be based on `absl::VerifyTypeImplementsAbslHashCorrectly` directly as we have a different type per
  // `tstring`, so we cannot create the necessary minimum of two equivalence classes. To overcome this we use them
  // indirectly wrapped in a `std::variant` that can hold all three types. This way the Abseil helper can generate
  // the equivalence classes. We also through in some string types of the same values.
  using TsVar = std::variant<std::string_view, decltype(kTsBar), decltype(kTsBaz), decltype(kTsFoo)>;
  constexpr TsVar kTsVarBar = kTsBar;
  constexpr TsVar kTsVarBaz = kTsBaz;
  constexpr TsVar kTsVarFoo = kTsFoo;
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      {kTsVarBar, kTsVarBaz, kTsVarFoo, TsVar("bar"), TsVar("baz"), TsVar("foo")}));
  // All 3 are different:
  EXPECT_THAT(absl::HashOf(kTsBar), Ne(absl::HashOf(kTsBaz)));
  EXPECT_THAT(absl::HashOf(kTsBar), Ne(absl::HashOf(kTsFoo)));
  EXPECT_THAT(absl::HashOf(kTsBaz), Ne(absl::HashOf(kTsFoo)));
  // Neither matches their `std::string_view` representation.
  EXPECT_THAT(absl::HashOf(kTsBar), Ne(absl::HashOf(decltype(kTsBar)::str())));
  EXPECT_THAT(absl::HashOf(kTsBar), Ne(absl::HashOf(std::string_view("bar"))));
  EXPECT_THAT(absl::HashOf(kTsBaz), Ne(absl::HashOf(decltype(kTsBaz)::str())));
  EXPECT_THAT(absl::HashOf(kTsBaz), Ne(absl::HashOf(std::string_view("baz"))));
  EXPECT_THAT(absl::HashOf(kTsFoo), Ne(absl::HashOf(decltype(kTsFoo)::str())));
  EXPECT_THAT(absl::HashOf(kTsFoo), Ne(absl::HashOf(std::string_view("foo"))));
}

// NOLINTEND(readability-static-definition-in-anonymous-namespace)
// NOLINTEND(readability-static-accessed-through-instance)
// NOLINTEND(hicpp-no-array-decay)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTEND(*-magic-numbers)
// NOLINTEND(*-avoid-c-arrays)

}  // namespace
}  // namespace mbo::types
