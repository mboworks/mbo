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

#include <cstddef>
#include <tuple>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/types/tuple_extras.h"

namespace mbo::types {
namespace {

using ::testing::Eq;

class DecomposeCountCoverageTest : public ::testing::Test {};

struct Aggregate1 {
  int field1;
};

struct Aggregate2 {
  int field1;
  int field2;
};

struct Aggregate3 {
  int field1;
  int field2;
  int field3;
};

struct Aggregate4 {
  int field1;
  int field2;
  int field3;
  int field4;
};

struct Aggregate5 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
};

struct Aggregate6 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
};

struct Aggregate7 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
};

struct Aggregate8 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
};

struct Aggregate9 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
};

struct Aggregate10 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
};

struct Aggregate11 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
};

struct Aggregate12 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
};

struct Aggregate13 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
};

struct Aggregate14 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
};

struct Aggregate15 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
};

struct Aggregate16 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
};

struct Aggregate17 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
};

struct Aggregate18 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
};

struct Aggregate19 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
};

struct Aggregate20 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
};

struct Aggregate21 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
};

struct Aggregate22 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
};

struct Aggregate23 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
};

struct Aggregate24 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
};

struct Aggregate25 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
};

struct Aggregate26 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
};

struct Aggregate27 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
};

struct Aggregate28 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
  int field28;
};

struct Aggregate29 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
  int field28;
  int field29;
};

struct Aggregate30 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
  int field28;
  int field29;
  int field30;
};

struct Aggregate31 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
  int field28;
  int field29;
  int field30;
  int field31;
};

struct Aggregate32 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
  int field28;
  int field29;
  int field30;
  int field31;
  int field32;
};

struct Aggregate33 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
  int field28;
  int field29;
  int field30;
  int field31;
  int field32;
  int field33;
};

struct Aggregate34 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
  int field28;
  int field29;
  int field30;
  int field31;
  int field32;
  int field33;
  int field34;
};

struct Aggregate35 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
  int field28;
  int field29;
  int field30;
  int field31;
  int field32;
  int field33;
  int field34;
  int field35;
};

struct Aggregate36 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
  int field28;
  int field29;
  int field30;
  int field31;
  int field32;
  int field33;
  int field34;
  int field35;
  int field36;
};

struct Aggregate37 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
  int field28;
  int field29;
  int field30;
  int field31;
  int field32;
  int field33;
  int field34;
  int field35;
  int field36;
  int field37;
};

struct Aggregate38 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
  int field28;
  int field29;
  int field30;
  int field31;
  int field32;
  int field33;
  int field34;
  int field35;
  int field36;
  int field37;
  int field38;
};

struct Aggregate39 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
  int field28;
  int field29;
  int field30;
  int field31;
  int field32;
  int field33;
  int field34;
  int field35;
  int field36;
  int field37;
  int field38;
  int field39;
};

struct Aggregate40 {
  int field1;
  int field2;
  int field3;
  int field4;
  int field5;
  int field6;
  int field7;
  int field8;
  int field9;
  int field10;
  int field11;
  int field12;
  int field13;
  int field14;
  int field15;
  int field16;
  int field17;
  int field18;
  int field19;
  int field20;
  int field21;
  int field22;
  int field23;
  int field24;
  int field25;
  int field26;
  int field27;
  int field28;
  int field29;
  int field30;
  int field31;
  int field32;
  int field33;
  int field34;
  int field35;
  int field36;
  int field37;
  int field38;
  int field39;
  int field40;
};

template<typename T>
void VerifyValueCategories(T value) {
  const T const_value = value;
  const auto mutable_tuple = StructToTuple(value);
  const auto const_tuple = StructToTuple(const_value);
  const auto moved_tuple = StructToTuple(std::move(value));
  constexpr std::size_t kLast = std::tuple_size_v<decltype(mutable_tuple)> - 1;

  EXPECT_THAT(std::get<0>(mutable_tuple), Eq(0));
  EXPECT_THAT(std::get<kLast>(const_tuple), Eq(0));
  EXPECT_THAT(std::get<kLast>(moved_tuple), Eq(0));
}

TEST_F(DecomposeCountCoverageTest, EverySupportedArityAndValueCategory) {
  VerifyValueCategories(Aggregate1{});
  VerifyValueCategories(Aggregate2{});
  VerifyValueCategories(Aggregate3{});
  VerifyValueCategories(Aggregate4{});
  VerifyValueCategories(Aggregate5{});
  VerifyValueCategories(Aggregate6{});
  VerifyValueCategories(Aggregate7{});
  VerifyValueCategories(Aggregate8{});
  VerifyValueCategories(Aggregate9{});
  VerifyValueCategories(Aggregate10{});
  VerifyValueCategories(Aggregate11{});
  VerifyValueCategories(Aggregate12{});
  VerifyValueCategories(Aggregate13{});
  VerifyValueCategories(Aggregate14{});
  VerifyValueCategories(Aggregate15{});
  VerifyValueCategories(Aggregate16{});
  VerifyValueCategories(Aggregate17{});
  VerifyValueCategories(Aggregate18{});
  VerifyValueCategories(Aggregate19{});
  VerifyValueCategories(Aggregate20{});
  VerifyValueCategories(Aggregate21{});
  VerifyValueCategories(Aggregate22{});
  VerifyValueCategories(Aggregate23{});
  VerifyValueCategories(Aggregate24{});
  VerifyValueCategories(Aggregate25{});
  VerifyValueCategories(Aggregate26{});
  VerifyValueCategories(Aggregate27{});
  VerifyValueCategories(Aggregate28{});
  VerifyValueCategories(Aggregate29{});
  VerifyValueCategories(Aggregate30{});
  VerifyValueCategories(Aggregate31{});
  VerifyValueCategories(Aggregate32{});
  VerifyValueCategories(Aggregate33{});
  VerifyValueCategories(Aggregate34{});
  VerifyValueCategories(Aggregate35{});
  VerifyValueCategories(Aggregate36{});
  VerifyValueCategories(Aggregate37{});
  VerifyValueCategories(Aggregate38{});
  VerifyValueCategories(Aggregate39{});
  VerifyValueCategories(Aggregate40{});
}

}  // namespace
}  // namespace mbo::types
