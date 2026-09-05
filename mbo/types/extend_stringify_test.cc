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

#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/log/absl_check.h"  // IWYU pragma: keep
#include "absl/log/absl_log.h"    // IWYU pragma: keep
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/container/limited_vector.h"
#include "mbo/testing/matchers.h"
#include "mbo/types/extend.h"
#include "mbo/types/extender.h"
#include "mbo/types/stringify.h"

#if defined(__clang__)
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wunknown-pragmas"
# pragma clang diagnostic ignored "-Wunused-local-typedef"
# // There is an issue with ADL members here.
# pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
# pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#endif  // defined(__clang__)

// Not using namespace mbo::types {
namespace {

// NOLINTBEGIN(*-magic-numbers,*-named-parameter)

using ::mbo::testing::EqualsText;
using ::mbo::types::Extend;
using ::mbo::types::HasMboTypesStringifyDoNotPrintFieldNames;
using ::mbo::types::HasMboTypesStringifyFieldNames;
using ::mbo::types::HasMboTypesStringifyOptions;
using ::mbo::types::Stringify;
using ::mbo::types::StringifyFieldInfo;
using ::mbo::types::StringifyNameHandling;
using ::mbo::types::StringifyOptions;
using ::mbo::types::StringifyWithFieldNames;
using ::mbo::types::types_internal::SupportsFieldNames;
using ::mbo::types::types_internal::SupportsFieldNamesConstexpr;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

MATCHER_P(HasFieldName, field_name, "") {
  return ::testing::ExplainMatchResult(field_name, arg, result_listener);
}

struct ExtenderStringifyTest : ::testing::Test {
  struct Tester {
    MOCK_METHOD(std::vector<std::string>, FieldNames, ());
    MOCK_METHOD(StringifyOptions, FieldOptions, (std::size_t, std::string_view));
  };

  ExtenderStringifyTest() { tester = new Tester(); }  // NOLINT(cppcoreguidelines-owning-memory)

  ExtenderStringifyTest(const ExtenderStringifyTest&) noexcept = delete;
  ExtenderStringifyTest& operator=(const ExtenderStringifyTest&) noexcept = delete;
  ExtenderStringifyTest(ExtenderStringifyTest&&) noexcept = delete;
  ExtenderStringifyTest& operator=(ExtenderStringifyTest&&) noexcept = delete;

  ~ExtenderStringifyTest() override {
    delete tester;  // NOLINT(cppcoreguidelines-owning-memory)
    tester = nullptr;
  }

  static Tester* tester;
};

ExtenderStringifyTest::Tester* ExtenderStringifyTest::tester = nullptr;

TEST_F(ExtenderStringifyTest, AllDataSet) {
  const StringifyOptions& opts = Stringify::OptionsDefault();
  EXPECT_TRUE(opts.format.has_value());
  EXPECT_TRUE(opts.field_control.has_value());
  EXPECT_TRUE(opts.key_control.has_value());
  EXPECT_TRUE(opts.key_overrides.has_value());
  EXPECT_TRUE(opts.value_control.has_value());
  EXPECT_TRUE(opts.value_overrides.has_value());
  EXPECT_TRUE(opts.special.has_value());
  EXPECT_TRUE(Stringify::OptionsDefault().AllDataSet());
}

TEST_F(ExtenderStringifyTest, AllExtensionApiPointsAbsent) {
  struct TestStruct : Extend<TestStruct> {
    int one = 11;
  };

  EXPECT_FALSE(HasMboTypesStringifyDoNotPrintFieldNames<TestStruct>);
  EXPECT_FALSE(HasMboTypesStringifyFieldNames<TestStruct>);
  EXPECT_FALSE(HasMboTypesStringifyOptions<TestStruct>);
}

TEST_F(ExtenderStringifyTest, SuppressFieldNames) {
  struct SuppressFieldNames : ::Extend<SuppressFieldNames> {
    using MboTypesStringifyDoNotPrintFieldNames = void;

    int one = 25;
    std::string_view two = "42";
  };

  ASSERT_TRUE(HasMboTypesStringifyDoNotPrintFieldNames<SuppressFieldNames>);
  EXPECT_FALSE(HasMboTypesStringifyFieldNames<SuppressFieldNames>);
  EXPECT_FALSE(HasMboTypesStringifyOptions<SuppressFieldNames>);
  EXPECT_THAT(SuppressFieldNames{}.ToString(), R"({25, "42"})")
      << "  NOTE: Here no compiler should print any field name.";
}

struct AddFieldNames : ::Extend<AddFieldNames> {
  int one = 25;
  std::string_view two = "42";

  friend auto MboTypesStringifyFieldNames(const AddFieldNames&) { return std::array<std::string_view, 2>{"1", "2"}; }
};

TEST_F(ExtenderStringifyTest, AddFieldNames) {
  // Proves the straight forward type: a `std::array<std::string, N>` works.
  ASSERT_FALSE(HasMboTypesStringifyDoNotPrintFieldNames<AddFieldNames>);
  ASSERT_TRUE(HasMboTypesStringifyFieldNames<AddFieldNames>);
  ASSERT_FALSE(HasMboTypesStringifyOptions<AddFieldNames>);
  EXPECT_THAT(AddFieldNames{}.ToString(), R"({.1: 25, .2: "42"})")
      << "  NOTE: Here we inject the field names and override any possibly compiler provided names.";
}

struct AddFieldVectorOfString : ::Extend<AddFieldVectorOfString> {
  int one = 25;
  std::string_view two = "42";

  friend auto MboTypesStringifyFieldNames(const AddFieldVectorOfString&) { return std::vector<std::string>{"1", "2"}; }
};

TEST_F(ExtenderStringifyTest, AddFieldVectorOfString) {
  // This test proves that conversion from temp strings works through lifetime extension.
  ASSERT_FALSE(HasMboTypesStringifyDoNotPrintFieldNames<AddFieldVectorOfString>);
  ASSERT_TRUE(HasMboTypesStringifyFieldNames<AddFieldVectorOfString>);
  ASSERT_FALSE(HasMboTypesStringifyOptions<AddFieldVectorOfString>);
  EXPECT_THAT(AddFieldVectorOfString{}.ToString(), R"({.1: 25, .2: "42"})")
      << "  NOTE: Here we inject the field names and override any possibly compiler provided names.";
}

struct AddFieldNamesLimitedVector : ::Extend<AddFieldNamesLimitedVector> {
  int one = 25;
  std::string_view two = "42";

  friend auto MboTypesStringifyFieldNames(const AddFieldNamesLimitedVector&) {
    return mbo::container::MakeLimitedVector("1", "2");
  }
};

TEST_F(ExtenderStringifyTest, AddFieldNamesLimitedVector) {
  // Proves other types can be compatible.
  ASSERT_FALSE(HasMboTypesStringifyDoNotPrintFieldNames<AddFieldNamesLimitedVector>);
  ASSERT_TRUE(HasMboTypesStringifyFieldNames<AddFieldNamesLimitedVector>);
  ASSERT_FALSE(HasMboTypesStringifyOptions<AddFieldNamesLimitedVector>);
  EXPECT_THAT(AddFieldNamesLimitedVector{}.ToString(), R"({.1: 25, .2: "42"})")
      << "  NOTE: Here we inject the field names and override any possibly compiler provided names.";
}

struct TestStructFieldOptions : Extend<TestStructFieldOptions> {
  int one = 11;
  std::pair<int, int> two = {25, 27};
  int three = 33;

  friend StringifyOptions MboTypesStringifyOptions(const TestStructFieldOptions&, const StringifyFieldInfo& field) {
    return ExtenderStringifyTest::tester->FieldOptions(field.idx, field.name);
  }
};

TEST_F(ExtenderStringifyTest, FieldOptions) {
  // Demonstrates different parameters can be routed differently.
  // The test verifies that:
  // * based on field index (or name if available) different control can be returned.
  // * fields `one` and `two` have different key control, including field name overriding.
  // * field `three` will be fully suppressed.
  ASSERT_FALSE(HasMboTypesStringifyDoNotPrintFieldNames<TestStructFieldOptions>);
  ASSERT_FALSE(HasMboTypesStringifyFieldNames<TestStructFieldOptions>);
  ASSERT_TRUE(HasMboTypesStringifyOptions<TestStructFieldOptions>);

  EXPECT_CALL(*tester, FieldOptions(0, HasFieldName("one")))
      .WillOnce(::testing::Return(StringifyOptions{
          .format{StringifyOptions::Format{
              .key_value_separator = "=1=",
              .field_separator = "+1+",
          }},
          .field_control{StringifyOptions::FieldControl{
              .suppress = false,
          }},
          .key_control{StringifyOptions::KeyControl{
              .key_prefix = "_1_",
              .key_suffix = ".1.",
          }},
          .key_overrides{StringifyOptions::KeyOverrides{
              .key_use_name = "first",
          }},
      }));
  EXPECT_CALL(*tester, FieldOptions(1, HasFieldName("two")))
      .WillOnce(::testing::Return(StringifyOptions{
          .format{StringifyOptions::Format{
              .key_value_separator = "=2=",
              .field_separator = "+2+",
          }},
          .field_control{StringifyOptions::FieldControl{
              .suppress = false,
          }},
          .key_control{StringifyOptions::KeyControl{
              .key_prefix = "_2_",
              .key_suffix = ".2.",
          }},
          .key_overrides{StringifyOptions::KeyOverrides{
              .key_use_name = "second",
          }},
      }));
  EXPECT_CALL(*tester, FieldOptions(2, HasFieldName("three")))
      .WillOnce(::testing::Return(StringifyOptions{
          .field_control{StringifyOptions::FieldControl{
              .suppress = true,
          }},
      }));

  EXPECT_THAT(TestStructFieldOptions{}.ToString(), "{_1_first.1.=1=11, _2_second.2.=2={.first: 25+2+.second: 27}}");
}

struct TestStructFieldNames : Extend<TestStructFieldNames> {
  int one = 11;
  std::pair<int, int> two = {25, 27};
  int three = 33;

  friend std::vector<std::string> MboTypesStringifyFieldNames(const TestStructFieldNames&) {
    return ExtenderStringifyTest::tester->FieldNames();
  }

  friend StringifyOptions MboTypesStringifyOptions(const TestStructFieldNames&, const StringifyFieldInfo& field) {
    return ExtenderStringifyTest::tester->FieldOptions(field.idx, field.name);
  }
};

TEST_F(ExtenderStringifyTest, FieldNames) {
  // Demonstrates different parameters can be routed differently.
  // Unlike test `FieldOptions` here we first fetch the field names which override compiler provided ones.
  ASSERT_FALSE(HasMboTypesStringifyDoNotPrintFieldNames<TestStructFieldNames>);
  ASSERT_TRUE(HasMboTypesStringifyFieldNames<TestStructFieldNames>);
  ASSERT_TRUE(HasMboTypesStringifyOptions<TestStructFieldNames>);

  // 1st ToString call.
  EXPECT_CALL(*tester, FieldNames()).WillOnce(::testing::Return(std::vector<std::string>{"First", "Second", "Third"}));
  EXPECT_CALL(*tester, FieldOptions(0, "First"))
      .WillOnce(::testing::Return(StringifyOptions{
          .format{StringifyOptions::Format{
              .key_value_separator = "=1=",
              .field_separator = "+1+",
          }},
          .key_control{StringifyOptions::KeyControl{
              .key_prefix = "_1_",
              .key_suffix = ".1.",
          }},
      }));
  EXPECT_CALL(*tester, FieldOptions(1, "Second"))
      .WillOnce(::testing::Return(StringifyOptions{
          .format{StringifyOptions::Format{
              .key_value_separator = "=2=",
              .field_separator = "+2+",
          }},
          .field_control{StringifyOptions::FieldControl{
              .suppress = true,
          }},
          .key_control{StringifyOptions::KeyControl{
              .key_prefix = "_2_",
              .key_suffix = ".2.",
          }},
      }));
  EXPECT_CALL(*tester, FieldOptions(2, "Third"))
      .WillOnce(::testing::Return(StringifyOptions{
          .format{StringifyOptions::Format{
              .key_value_separator = "=3=",
              .field_separator = "+3+",
          }},
          .key_control{StringifyOptions::KeyControl{
              .key_prefix = "_3_",
              .key_suffix = ".3.",
          }},
      }));

  EXPECT_THAT(TestStructFieldNames{}.ToString(), "{_1_First.1.=1=11, _3_Third.3.=3=33}");

  // 2nd ToString call.
  EXPECT_CALL(*tester, FieldNames()).WillOnce(::testing::Return(std::vector<std::string>{"Fourth"}));
  EXPECT_CALL(*tester, FieldOptions(0, "Fourth"))
      .WillOnce(::testing::Return(StringifyOptions{
          .format{StringifyOptions::Format{
              .key_value_separator = "=4=",
              .field_separator = "+4+",
          }},
          .key_control{StringifyOptions::KeyControl{
              .key_prefix = "_4_",
              .key_suffix = ".4.",
          }},
      }));
  // 2nd and 3rd field get printed. But 2nd has no key name, so related options get ignored.
  EXPECT_CALL(*tester, FieldOptions(1, IsEmpty()))
      .WillOnce(::testing::Return(StringifyOptions{
          .format{StringifyOptions::Format{
              .key_value_separator = "=5=",
              .field_separator = "+5+",
          }},
          .key_control{StringifyOptions::KeyControl{
              .key_prefix = "_5_",
              .key_suffix = ".5.",
          }},
      }));
  // For the 3rd field, the options provide the field name through `key_use_name`.
  EXPECT_CALL(*tester, FieldOptions(2, IsEmpty()))
      .WillOnce(::testing::Return(StringifyOptions{
          .format{StringifyOptions::Format{
              .key_value_separator = "=6=",
              .field_separator = "+6+",
          }},
          .key_control{StringifyOptions::KeyControl{
              .key_prefix = "_6_",
              .key_suffix = ".6.",
          }},
          .key_overrides{StringifyOptions::KeyOverrides{
              .key_use_name = "Sixth",
          }},
      }));
  EXPECT_THAT(TestStructFieldNames{}.ToString(), "{_4_Fourth.4.=4=11, {.first: 25+5+.second: 27}, _6_Sixth.6.=6=33}");
}

struct TestStructDoNotPrintFieldNames : Extend<TestStructDoNotPrintFieldNames> {
  int one = 11;
  std::pair<int, int> two = {25, 27};
  int three = 33;

  using MboTypesStringifyDoNotPrintFieldNames = void;

  // It is not allowed to also have the API extension point `MboTypesStringifyFieldNames`.

  friend StringifyOptions MboTypesStringifyOptions(
      const TestStructDoNotPrintFieldNames&,
      const StringifyFieldInfo& field) {
    return ExtenderStringifyTest::tester->FieldOptions(field.idx, field.name);
  }
};

TEST_F(ExtenderStringifyTest, DoNotPrintFieldNames) {
  // Demonstrates different parameters can be routed differently.
  // Unlike test `FieldOptions` here we first fetch the field names which override compiler provided ones.
  ASSERT_TRUE(HasMboTypesStringifyDoNotPrintFieldNames<TestStructDoNotPrintFieldNames>);
  ASSERT_FALSE(HasMboTypesStringifyFieldNames<TestStructDoNotPrintFieldNames>);
  ASSERT_TRUE(HasMboTypesStringifyOptions<TestStructDoNotPrintFieldNames>);

  EXPECT_CALL(*tester, FieldOptions(0, IsEmpty()))
      .WillOnce(::testing::Return(StringifyOptions{
          .format{StringifyOptions::Format{
              .key_value_separator = "==",
              .field_separator = "++",
          }},
          .field_control{StringifyOptions::FieldControl{
              .suppress = false,
          }},
          .key_control{StringifyOptions::KeyControl{
              .key_prefix = "__",
              .key_suffix = "..",
          }},
      }));
  EXPECT_CALL(*tester, FieldOptions(1, IsEmpty()))
      .WillOnce(::testing::Return(StringifyOptions{
          .format{StringifyOptions::Format{
              .key_value_separator = "==",
              .field_separator = "++",
          }},
          .field_control{StringifyOptions::FieldControl{
              .suppress = false,
          }},
          .key_control{StringifyOptions::KeyControl{
              .key_prefix = "__",
              .key_suffix = "..",
          }},
      }));
  EXPECT_CALL(*tester, FieldOptions(2, IsEmpty()))
      .WillOnce(::testing::Return(StringifyOptions{
          .field_control{StringifyOptions::FieldControl{
              .suppress = true,
          }},
      }));
  EXPECT_THAT(TestStructDoNotPrintFieldNames{}.ToString(), "{11, {25++27}}");
}

struct TestStructShorten : Extend<TestStructShorten> {
  std::string_view one = "1";
  std::string_view two = "22";
  std::string_view three = "333";
  std::string_view four = "4444";
  std::string_view five;

  friend StringifyOptions MboTypesStringifyOptions(const TestStructShorten& v, const StringifyFieldInfo& field) {
    StringifyOptions opts = StringifyWithFieldNames({"one", "two", "three", "four", "five"})(v, field);
    StringifyOptions::Format& format = opts.format.as_data();
    format.key_value_separator = " = ";
    StringifyOptions::KeyControl& keys = opts.key_control.as_data();
    keys.key_prefix = "";
    StringifyOptions::ValueControl& vals = opts.value_control.as_data();
    vals.str_max_length = field.idx >= 3 && field.idx <= 4 ? 0U : 1U;
    vals.str_cutoff_suffix = field.idx < 2 ? Stringify::OptionsDefault().value_control->str_cutoff_suffix : "**";
    return opts;
  }
};

TEST_F(ExtenderStringifyTest, Shorten) {
  ASSERT_TRUE(HasMboTypesStringifyOptions<TestStructShorten>);
  EXPECT_THAT(TestStructShorten{}.ToString(), R"({one = "1", two = "2...", three = "3**", four = "**", five = ""})");
}

struct TestStructValueReplacement : Extend<TestStructValueReplacement> {
  int one = 1;
  std::string_view two = "22";
  std::vector<int> three = {331, 332, 333};
  std::vector<std::string_view> four{"41", "42", "43"};

  friend auto MboTypesStringifyOptions(const TestStructValueReplacement& v, const StringifyFieldInfo& field) {
    StringifyOptions opts = StringifyWithFieldNames({"one", "two", "three", "four"})(v, field);
    StringifyOptions::Format& format = opts.format.as_data();
    format.key_value_separator = " = ";
    StringifyOptions::KeyControl& keys = opts.key_control.as_data();
    keys.key_prefix = "";
    StringifyOptions::ValueOverrides& vals = opts.value_overrides.as_data();
    vals.replacement_str = "<XX>";
    vals.replacement_other = "<YY>";
    return opts;
  }
};

TEST_F(ExtenderStringifyTest, ValueReplacement) {
  ASSERT_TRUE(HasMboTypesStringifyOptions<TestStructValueReplacement>);
  EXPECT_THAT(
      TestStructValueReplacement{}.ToString(),
      R"({one = <YY>, two = "<XX>", three = {<YY>, <YY>, <YY>}, four = {"<XX>", "<XX>", "<XX>"}})");
}

struct TestStructContainer : Extend<TestStructContainer> {
  std::vector<int> one = {1, 2, 3};
  std::vector<int> two;
  std::vector<int> three = {1, 2, 3};

  friend StringifyOptions MboTypesStringifyOptions(const TestStructContainer& v, const StringifyFieldInfo& field) {
    StringifyOptions opts =
        StringifyWithFieldNames({"one", "two", "three"}, StringifyNameHandling::kOverwrite)(v, field);
    StringifyOptions::Format& format = opts.format.as_data();
    format.key_value_separator = " = ";
    format.container_prefix = "[";
    format.container_suffix = "]";
    StringifyOptions::KeyControl& keys = opts.key_control.as_data();
    keys.key_prefix = "";
    StringifyOptions::ValueControl& vals = opts.value_control.as_data();
    vals.container_max_len = field.idx == 1 ? 0U : 2U;
    return opts;
  }
};

TEST_F(ExtenderStringifyTest, Container) {
  ASSERT_TRUE(HasMboTypesStringifyOptions<TestStructContainer>);
  EXPECT_THAT(TestStructContainer{}.ToString(), R"({one = [1, 2], two = [], three = [1, 2]})");
}

struct TestStructMoreTypes : Extend<TestStructMoreTypes> {
  float one = 1.1;
  double two = 2.2;
  unsigned three = 3;
  char four = '4';
  unsigned char five = '5';

  friend auto MboTypesStringifyFieldNames(const TestStructMoreTypes&) {
    return std::array<std::string_view, 5>{"one", "two", "three", "four", "five"};
  }
};

TEST_F(ExtenderStringifyTest, MoreTypes) {
  ASSERT_TRUE(HasMboTypesStringifyFieldNames<TestStructMoreTypes>);
  ASSERT_FALSE(HasMboTypesStringifyOptions<TestStructMoreTypes>);
  EXPECT_THAT(TestStructMoreTypes{}.ToString(), R"({.one: 1.1, .two: 2.2, .three: 3, .four: '4', .five: '5'})");
}

struct TestStructMoreContainers : Extend<TestStructMoreContainers> {
  std::set<int> one = {1, 2};
  std::map<int, int> two = {{1, 2}, {3, 4}};
  std::vector<std::pair<int, int>> three = {{5, 6}};

  friend StringifyOptions MboTypesStringifyOptions(const TestStructMoreContainers& v, const StringifyFieldInfo& field) {
    StringifyOptions ret = StringifyWithFieldNames({"one", "two", "three", "four"})(
        v, StringifyFieldInfo{.options{Stringify::OptionsJson()}, .idx = field.idx, .name = field.name});
    if (field.idx == 2) {
      ret.special.as_data().pair_keys = {{"Key", "Val"}};
    }
    return ret;
  }
};

TEST_F(ExtenderStringifyTest, MoreContainers) {
  ASSERT_TRUE(HasMboTypesStringifyOptions<TestStructMoreContainers>);
  EXPECT_THAT(
      TestStructMoreContainers{}.ToString(),
      R"({"one":[1,2], "two":[{.first: 1,.second: 2},{.first: 3,.second: 4}], "three":[{.Key: 5,.Val: 6}]})")
      << "  NOTE: Here we are not providing the default Json options down do the pairs. "
         "However, in `three` we have "
         "the provided key/value names.";
  EXPECT_THAT(
      TestStructMoreContainers{}.ToString(Stringify::OptionsJson()),
      R"({"one":[1,2],"two":[{"first":1,"second":2},{"first":3,"second":4}],"three":[{"Key":5,"Val":6}]}
)");
}

struct TestStructMoreContainersWithDirectFieldNames : Extend<TestStructMoreContainersWithDirectFieldNames> {
  std::set<int> one = {1, 2};
  std::map<int, int> two = {{1, 2}, {3, 4}};
  std::vector<std::pair<int, int>> three = {{5, 6}};

  friend auto MboTypesStringifyFieldNames(const TestStructMoreContainersWithDirectFieldNames&) {
    return std::array<std::string_view, 3>{"1", "2", "3"};
  }

  friend StringifyOptions MboTypesStringifyOptions(
      const TestStructMoreContainersWithDirectFieldNames&,
      const StringifyFieldInfo& field) {
    StringifyOptions ret = field.options.inner;
    if (field.idx == 2) {
      ret.special.as_data().pair_keys = {{"Key", "Val"}};
    }
    return ret;
  }
};

TEST_F(ExtenderStringifyTest, MoreContainersWithDirectFieldNames) {
  static_assert(!HasMboTypesStringifyDoNotPrintFieldNames<TestStructMoreContainersWithDirectFieldNames>);
  static_assert(HasMboTypesStringifyOptions<TestStructMoreContainersWithDirectFieldNames>);
  static_assert(HasMboTypesStringifyFieldNames<decltype(TestStructMoreContainersWithDirectFieldNames{})>);
  EXPECT_TRUE(HasMboTypesStringifyFieldNames<decltype(TestStructMoreContainersWithDirectFieldNames{})>);
  EXPECT_THAT(MboTypesStringifyFieldNames(TestStructMoreContainersWithDirectFieldNames{}), ElementsAre("1", "2", "3"));
  EXPECT_THAT(
      TestStructMoreContainersWithDirectFieldNames{}.ToString(Stringify::OptionsJson()),
      R"({"1":[1,2],"2":[{"first":1,"second":2},{"first":3,"second":4}],"3":[{"Key":5,"Val":6}]}
)");
}

struct TestStructContainersOfPairs : Extend<TestStructContainersOfPairs> {
  std::map<std::string_view, int> one = {{"a", 1}, {"b", 2}};
  std::vector<std::pair<std::string_view, int>> two = {{"c", 3}, {"d", 4}};

  friend StringifyOptions MboTypesStringifyOptions(
      const TestStructContainersOfPairs& v,
      const StringifyFieldInfo& field) {
    return StringifyWithFieldNames({"one", "two", "three", "four"})(
        v, StringifyFieldInfo{.options{Stringify::OptionsJson()}, .idx = field.idx, .name = field.name});
  }
};

TEST_F(ExtenderStringifyTest, ContainersOfPairs) {
  ASSERT_TRUE(HasMboTypesStringifyOptions<TestStructContainersOfPairs>);
  EXPECT_THAT(TestStructContainersOfPairs{}.ToString(), R"({"one":{"a":1,"b":2}, "two":{"c":3,"d":4}})");
}

TEST_F(ExtenderStringifyTest, PrintWithControl) {
  struct TestStruct : Extend<TestStruct> {
    int one = 25;
  };

  const TestStruct v;
  EXPECT_THAT(v.ToString(Stringify::OptionsCpp()), R"({.one = 25})");
  EXPECT_THAT(v.ToString(Stringify::OptionsJson()), R"({"one":25}
)");
}

TEST_F(ExtenderStringifyTest, NestedDefaults) {
  struct TestSub : Extend<TestSub> {
    int four = 42;
  };

  struct TestStruct : Extend<TestStruct> {
    int one = 11;
    int two = 25;
    TestSub three = {.four = 42};
  };

  const TestStruct v;

  static constexpr std::string_view kExpectedDef = R"({.one: 11, .two: 25, .three: {.four: 42}})";
  static constexpr std::string_view kExpectedCpp = R"({.one = 11, .two = 25, .three = {.four = 42}})";
  static constexpr std::string_view kExpectedCppPretty = R"({
  .one = 11,
  .two = 25,
  .three = {
    .four = 42
  }
}
)";
  static constexpr std::string_view kExpectedJson = R"({"one":11,"two":25,"three":{"four":42}}
)";
  static constexpr std::string_view kExpectedJsonPretty = R"({
  "one": 11,
  "two": 25,
  "three": {
    "four": 42
  }
}
)";
  EXPECT_THAT(v.ToString(), kExpectedDef);
  EXPECT_THAT(v.ToString(Stringify::OptionsCpp()), kExpectedCpp);
  EXPECT_THAT(v.ToString(Stringify::OptionsCppPretty()), EqualsText(kExpectedCppPretty));
  EXPECT_THAT(v.ToString(Stringify::OptionsJson()), kExpectedJson);
  EXPECT_THAT(v.ToString(Stringify::OptionsJsonPretty()), EqualsText(kExpectedJsonPretty));
  EXPECT_THAT(v.ToJsonString(), kExpectedJson);
}

TEST_F(ExtenderStringifyTest, NestedJsonNumericFallback) {
  struct TestSub : Extend<TestSub> {
    int four = 42;

    using MboTypesStringifyDoNotPrintFieldNames = void;
  };

  struct TestStruct : Extend<TestStruct> {
    int one = 11;
    int two = 25;
    TestSub three = {.four = 42};

    using MboTypesStringifyDoNotPrintFieldNames = void;
  };

  static constexpr std::string_view kExpectedCpp = R"({11, 25, {42}})";
  static constexpr std::string_view kExpectedJson = R"({"0":11,"1":25,"2":{"0":42}}
)";
  EXPECT_THAT(TestStruct{}.ToString(Stringify::OptionsCpp()), kExpectedCpp);
  EXPECT_THAT(TestStruct{}.ToString(Stringify::OptionsJson()), kExpectedJson);
  EXPECT_THAT(TestStruct{}.ToJsonString(), kExpectedJson);
}

struct TestStructCustomNestedJsonNested : Extend<TestStructCustomNestedJsonNested> {
  int first = 0;
  std::string second = "nested";

  friend StringifyOptions MboTypesStringifyOptions(
      const TestStructCustomNestedJsonNested& v,
      const StringifyFieldInfo& field) {
    return StringifyWithFieldNames({"NESTED_1", "NESTED_2"}, StringifyNameHandling::kOverwrite)(
        v, StringifyFieldInfo{.options{Stringify::OptionsJson()}, .idx = field.idx, .name = field.name});
  }
};

struct TestStructCustomNestedJson : Extend<TestStructCustomNestedJson> {
  int one = 123;
  std::string two = "test";
  std::array<bool, 2> three = {false, true};
  std::vector<TestStructCustomNestedJsonNested> four = {{.first = 25, .second = "foo"}, {.first = 42, .second = "bar"}};
  std::pair<int, int> five = {25, 42};

  friend StringifyOptions MboTypesStringifyOptions(
      const TestStructCustomNestedJson& v,
      const StringifyFieldInfo& field) {
    return StringifyWithFieldNames({"one", "two", "three", "four", "five"})(
        v, StringifyFieldInfo{.options{Stringify::OptionsJson()}, .idx = field.idx, .name = field.name});
  }
};

TEST_F(ExtenderStringifyTest, CustomNestedJson) {
  ASSERT_TRUE(HasMboTypesStringifyOptions<TestStructCustomNestedJson>);

  // Keys for nested "four" should get their key names as "first" and "second" since they are
  // not provided. No handover of concrete values to defaults should occur. BUT: Five uses non
  // JSON mode as we fallback to the default options which were not set.
  EXPECT_THAT(
      TestStructCustomNestedJson{}.ToString(),
      R"({"one":123, "two":"test", "three":[false,true], "four":[{"NESTED_1":25,"NESTED_2":"foo"},{"NESTED_1":42,"NESTED_2":"bar"}], "five":{.first: 25,.second: 42}})");

  EXPECT_THAT(
      TestStructCustomNestedJson{}.ToString(Stringify::OptionsJson()),
      R"({"one":123,"two":"test","three":[false,true],"four":[{"NESTED_1":25,"NESTED_2":"foo"},{"NESTED_1":42,"NESTED_2":"bar"}],"five":{"first":25,"second":42}}
)");
}

struct TestStructNonLiteralFields : mbo::types::Extend<TestStructNonLiteralFields> {
  std::map<int, int> one = {{1, 2}, {2, 3}};
  std::unordered_map<int, int> two = {{3, 4}};
  std::string three = "three";

  friend auto MboTypesStringifyOptions(const TestStructNonLiteralFields& v, const StringifyFieldInfo& field) {
    return StringifyWithFieldNames({"one", "two", "three"}, StringifyNameHandling::kVerify)(v, field);
  }
};

TEST_F(ExtenderStringifyTest, NonLiteralFields) {
  ASSERT_TRUE(SupportsFieldNames<TestStructNonLiteralFields>);
#if defined(__clang__)
  ASSERT_FALSE(SupportsFieldNamesConstexpr<TestStructNonLiteralFields>);
#elif defined(__GNUC__)
  ASSERT_TRUE(SupportsFieldNamesConstexpr<TestStructNonLiteralFields>);
#else
# error Unsupported compiler
#endif
  ASSERT_FALSE(HasMboTypesStringifyDoNotPrintFieldNames<TestStructNonLiteralFields>);
  ASSERT_FALSE(HasMboTypesStringifyFieldNames<TestStructNonLiteralFields>);
  ASSERT_TRUE(HasMboTypesStringifyOptions<TestStructNonLiteralFields>);

  EXPECT_THAT(
      TestStructNonLiteralFields{}.ToString(Stringify::OptionsCpp()),
      R"({.one = {{.first = 1, .second = 2}, {.first = 2, .second = 3}}, .two = {{.first = 3, .second = 4}}, .three = "three"})");
}

struct TestExtApiCombo : mbo::types::Extend<TestExtApiCombo> {
  std::string one = "Once";

  friend constexpr auto MboTypesStringifyFieldNames(const TestExtApiCombo& /* unused */) {
    return std::array<std::string_view, 1>{"one"};
  }

  friend const StringifyOptions& MboTypesStringifyOptions(const TestExtApiCombo&, const StringifyFieldInfo&) {
    return Stringify::OptionsCpp();
  }
};

TEST_F(ExtenderStringifyTest, TestExtApiCombo) {
  EXPECT_THAT(TestExtApiCombo{}.ToString(), R"({.one = "Once"})");
}

struct TestSmartPtr : Extend<TestSmartPtr> {
  friend auto MboTypesStringifyFieldNames(const T&) {
    return std::array<std::string_view, 5>{"ups", "upn", "psv", "pn", "npt"};
  }

  static constexpr std::string_view kInit = "global";

  std::unique_ptr<std::string> ups;
  const std::unique_ptr<std::string> upn;
  const std::string_view* psv = &kInit;
  const int* pn = nullptr;
  const std::nullptr_t npt;
};

TEST_F(ExtenderStringifyTest, TestSmartPtr) {
  const TestSmartPtr val{.ups = std::make_unique<std::string>("foo")};

  EXPECT_THAT(
      val.ToString(), R"({.ups: {"foo"}, .upn: <nullptr>, .psv: *{"global"}, .pn: <nullptr>, .npt: std::nullptr_t})");
  EXPECT_THAT(
      val.ToString(Stringify::OptionsCpp()),
      R"({.ups = {"foo"}, .upn = nullptr, .psv = "global", .pn = nullptr, .npt = nullptr})");
  EXPECT_THAT(val.ToString(Stringify::OptionsJson()), R"({"ups":"foo","psv":"global"}
)");

  const TestSmartPtr val2{.ups = nullptr};
  EXPECT_THAT(val2.ToString(Stringify::OptionsJson()), R"({"psv":"global"}
)");
}  // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

struct TestOptional : Extend<TestOptional> {
  friend auto MboTypesStringifyFieldNames(const T&) { return std::array<std::string_view, 2>{"opt", "none"}; }

  std::optional<std::string_view> opt = "foo";
  const std::optional<std::string> none;
};

TEST_F(ExtenderStringifyTest, TestOptional) {
  const TestOptional val{};

  EXPECT_THAT(val.ToString(), R"({.opt: {"foo"}, .none: std::nullopt})");
  EXPECT_THAT(val.ToString(Stringify::OptionsCpp()), R"({.opt = {"foo"}, .none = std::nullopt})");
  EXPECT_THAT(val.ToString(Stringify::OptionsJson()), R"({"opt":"foo"}
)");
}

struct TestStringifyDisable : Extend<TestStringifyDisable> {
  struct Sub {
    using MboTypesStringifyDisable = void;

    int one = 42;
  };

  struct None {
    using MboTypesStringifySupport = void;
    using MboTypesStringifyDisable = void;

    int one = 42;
  };

  friend auto MboTypesStringifyFieldNames(const T&) { return std::array<std::string_view, 1>{"sub"}; }

  Sub sub;
};

TEST_F(ExtenderStringifyTest, TestStringifyDisable) {
  const TestStringifyDisable val{};

  EXPECT_THAT(val.ToString(), R"({.sub: {/*MboTypesStringifyDisable*/}})");
  EXPECT_THAT(val.ToString(Stringify::OptionsCpp()), R"({.sub = {/*MboTypesStringifyDisable*/}})");
  EXPECT_THAT(val.ToString(Stringify::OptionsJson()), R"({}
)");

  const TestStringifyDisable::None none{};
  EXPECT_THAT(mbo::types::Stringify().ToString(none), R"({/*MboTypesStringifyDisable*/})");
  EXPECT_THAT(mbo::types::Stringify::AsCpp().ToString(none), R"({/*MboTypesStringifyDisable*/})");
  EXPECT_THAT(mbo::types::Stringify::AsJson().ToString(none), R"(
)");
}

// NOLINTEND(*-magic-numbers,*-named-parameter)

}  // namespace

// Not using namespace mbo::types

#if defined(__clang__)
# pragma clang diagnostic pop
#elif defined(__GNUC__)
# pragma GCC diagnostic pop
#endif  // defined(__clang__)
