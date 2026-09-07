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

#include "mbo/mope/mope.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/mope/ini.h"

// Unit tests for the mope Template engine and its ini loader. The golden
// `*_diff_test`s exercise the binary end to end; these pin the LIBRARY contract:
// what SetValue/AddSection/Expand and ReadIniToTemlate do, one behaviour each.

namespace mbo::mope {

struct MopeTest : ::testing::Test {
  static std::string StreamSectionData() {
    return StreamData(Template::TagData<Template::Section>{.tag = {}, .data = {}});
  }

  static std::string StreamRangeData() { return StreamData(Template::TagData<Template::Range>{.tag = {}, .data = {}}); }

  static std::string StreamStringData() {
    return StreamData(Template::TagData<std::string>{.tag = {}, .data = "value"});
  }

  static absl::Status ExpandZeroStepRange() {
    const Template tpl;
    Template::Range range{.step = 0};
    Template::Context context;
    std::string output;
    return tpl.ExpandRangeTag({}, range, context, output);
  }

  static absl::Status ExpandDuplicateActiveRange() {
    const Template tpl;
    const Template::TagInfo tag{.name = "index", .type = Template::TagType::kSection};
    const Template::RangeData range{.start = "1", .end = "1"};
    Template::Context context;
    context.data.emplace("index", Template::TagData<std::string>{.tag = {}, .data = "active"});
    std::string output;
    return tpl.ExpandRangeData(tag, range, context, output);
  }

  static bool ExistsOnlyInContext() {
    const Template tpl;
    Template::Context context;
    context.data.emplace("external", Template::TagData<std::string>{.tag = {}, .data = "value"});
    return tpl.Exists("external", context);
  }

  static absl::Status LookupQuotedLiteral(std::string_view input) {
    const Template tpl;
    const Template::TagInfo tag{.name = "value"};
    const Template::Context context;
    std::string output;
    return tpl.MaybeLookup(tag, input, context, output);
  }

  static absl::Status ExpandEmptyInternal() {
    const Template tpl;
    Template::Context context;
    std::string output;
    return tpl.ExpandInternal(context, output);
  }

 private:
  static std::string StreamData(const Template::Data& data) {
    std::ostringstream out;
    out << data;
    return std::move(out).str();
  }
};

namespace {

using ::testing::IsFalse;
using ::testing::IsTrue;

namespace fs = std::filesystem;

TEST_F(MopeTest, DiagnosticDataStreamingCoversEveryAlternative) {
  EXPECT_THAT(StreamSectionData(), "Section");
  EXPECT_THAT(StreamRangeData(), "Range");
  EXPECT_THAT(StreamStringData(), "String: 'value'");
}

TEST_F(MopeTest, InternalDefensesAndContextPaths) {
  EXPECT_THAT(ExpandZeroStepRange().code(), absl::StatusCode::kInternal);
  EXPECT_THAT(ExpandDuplicateActiveRange().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(ExistsOnlyInContext(), IsTrue());
  EXPECT_THAT(LookupQuotedLiteral(R"("literal")"), absl::OkStatus());
  EXPECT_THAT(LookupQuotedLiteral(R"('literal')"), absl::OkStatus());
  EXPECT_THAT(ExpandEmptyInternal(), absl::OkStatus());
}

TEST_F(MopeTest, ExpandReplacesASetValue) {
  constexpr std::string_view kInput = "x {{foo}} y";
  Template tpl;
  ASSERT_THAT(tpl.SetValue("foo", "bar"), absl::OkStatus());
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "x bar y");
}

TEST_F(MopeTest, SetValueRefusesToOverwriteWithoutAllowUpdate) {
  constexpr std::string_view kInput = "{{foo}}";
  Template tpl;
  ASSERT_THAT(tpl.SetValue("foo", "bar"), absl::OkStatus());
  EXPECT_THAT(tpl.SetValue("foo", "baz").ok(), IsFalse()) << "silent overwrite would hide a template bug";
  ASSERT_THAT(tpl.SetValue("foo", "baz", /*allow_update=*/true), absl::OkStatus());
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "baz");
}

TEST_F(MopeTest, InlineAssignmentSetsAndExpands) {
  constexpr std::string_view kInput = "{{v=x}}{{v}}";
  // The template language's own assignment: `{{name=value}}` emits nothing and
  // defines `name` for the rest of the expansion (see tests/simply.mope).
  const Template tpl;
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "x");
}

TEST_F(MopeTest, IsValidNameRejectsWhatSetValueWouldChokeOn) {
  EXPECT_THAT(Template::IsValidName("foo"), IsTrue());
  EXPECT_THAT(Template::IsValidName("foo_bar"), IsTrue());
  EXPECT_THAT(Template::IsValidName(""), IsFalse());
  EXPECT_THAT(Template::IsValidName("a b"), IsFalse());
}

TEST_F(MopeTest, SetValueAndAddSectionRejectInvalidNames) {
  Template tpl;
  EXPECT_THAT(tpl.SetValue("not valid", "value").code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(tpl.AddSection("9invalid").status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(MopeTest, SetValueCannotReplaceASection) {
  Template tpl;
  ASSERT_THAT(tpl.AddSection("item").ok(), IsTrue());
  EXPECT_THAT(tpl.SetValue("item", "value", /*allow_update=*/true).code(), absl::StatusCode::kAlreadyExists);
}

TEST_F(MopeTest, AddSectionCannotReplaceAValue) {
  Template tpl;
  ASSERT_THAT(tpl.SetValue("item", "value"), absl::OkStatus());
  EXPECT_THAT(tpl.AddSection("item").status().code(), absl::StatusCode::kAlreadyExists);
}

TEST_F(MopeTest, ExpandUsesContextWithoutChangingTemplateValues) {
  constexpr std::string_view kInput = "{{local}}/{{external}}";
  Template tpl;
  ASSERT_THAT(tpl.SetValue("local", "template"), absl::OkStatus());
  const std::array<std::pair<std::string_view, std::string_view>, 2> context = {
      std::pair{"external", "context"},
      std::pair{"other", "unused"},
  };
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output, mbo::container::MakeConvertingScan(context)), absl::OkStatus());
  EXPECT_THAT(output, "template/context");
}

TEST_F(MopeTest, ExpandRejectsDuplicateContextValues) {
  const Template tpl;
  const std::array<std::pair<std::string_view, std::string_view>, 2> context = {
      std::pair{"duplicate", "first"},
      std::pair{"duplicate", "second"},
  };
  std::string output;
  EXPECT_THAT(tpl.Expand(output, mbo::container::MakeConvertingScan(context)).code(), absl::StatusCode::kAlreadyExists);
}

TEST_F(MopeTest, ExpandLeavesAnUnknownValueTagUnchanged) {
  constexpr std::string_view kInput = "before {{unknown}} after";
  const Template tpl;
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "before {{unknown}} after");
}

TEST_F(MopeTest, ExpandRejectsASectionWithoutAnEndTag) {
  constexpr std::string_view kInput = "{{#item}}content";
  const Template tpl;
  std::string output(kInput);
  EXPECT_THAT(tpl.Expand(output).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(MopeTest, StandaloneSectionTagsConsumeTheirLines) {
  constexpr std::string_view kInput = "before\n  {{#missing}}\ncontent\n  {{/missing}}\nafter\n";
  const Template tpl;
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "before\nafter\n");
}

TEST_F(MopeTest, StandaloneSectionTagsRecognizeCarriageReturnsAndTabs) {
  constexpr std::string_view kInput = "before\r\t{{#missing}}\rcontent\r\t{{/missing}}\rafter\r";
  const Template tpl;
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "before\rafter\r");
}

TEST_F(MopeTest, InlineSectionTagsKeepTheirSurroundingLines) {
  constexpr std::string_view kInput = "before {{#missing}}\ncontent\n{{/missing}} after\n";
  const Template tpl;
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "before  after\n");
}

TEST_F(MopeTest, MissingSectionsDisappearAndAnEmptyDictionaryRendersOnce) {
  constexpr std::string_view kMissingInput = "before{{#missing}}content{{/missing}}after";
  constexpr std::string_view kEmptyInput = "before{{#empty}}content{{/empty}}after";
  Template tpl;
  std::string missing(kMissingInput);
  ASSERT_THAT(tpl.Expand(missing), absl::OkStatus());
  EXPECT_THAT(missing, "beforeafter");

  ASSERT_THAT(tpl.AddSection("empty").ok(), IsTrue());
  std::string empty(kEmptyInput);
  ASSERT_THAT(tpl.Expand(empty), absl::OkStatus());
  EXPECT_THAT(empty, "beforecontentafter");
}

TEST_F(MopeTest, SectionsExpandEveryDictionaryWithAJoiner) {
  constexpr std::string_view kInput = R"({{#item:", "}}{{value}}{{/item}})";
  Template tpl;
  auto first = tpl.AddSection("item");
  ASSERT_THAT(first.ok(), IsTrue());
  ASSERT_THAT((*first)->SetValue("value", "one"), absl::OkStatus());
  auto second = tpl.AddSection("item");
  ASSERT_THAT(second.ok(), IsTrue());
  ASSERT_THAT((*second)->SetValue("value", "two"), absl::OkStatus());

  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "one, two");
}

TEST_F(MopeTest, SectionCannotUseAValueAsADictionary) {
  constexpr std::string_view kInput = "{{#item}}content{{/item}}";
  Template tpl;
  ASSERT_THAT(tpl.SetValue("item", "value"), absl::OkStatus());
  std::string output(kInput);
  EXPECT_THAT(tpl.Expand(output).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(MopeTest, RangeExpandsInBothDirections) {
  constexpr std::string_view kAscendingInput = R"({{#index=1;3;;","}}{{index}}{{/index}})";
  constexpr std::string_view kDescendingInput = R"({{#index=3;1;-1;":"}}{{index}}{{/index}})";
  const Template tpl;
  std::string ascending(kAscendingInput);
  ASSERT_THAT(tpl.Expand(ascending), absl::OkStatus());
  EXPECT_THAT(ascending, "1,2,3");

  std::string descending(kDescendingInput);
  ASSERT_THAT(tpl.Expand(descending), absl::OkStatus());
  EXPECT_THAT(descending, "3:2:1");
}

TEST_F(MopeTest, RangeOperandsCanReferenceTemplateValues) {
  constexpr std::string_view kInput = "{{#index=first;last}}{{index}}{{/index}}";
  Template tpl;
  ASSERT_THAT(tpl.SetValue("first", "2"), absl::OkStatus());
  ASSERT_THAT(tpl.SetValue("last", "4"), absl::OkStatus());
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "234");
}

TEST_F(MopeTest, NestedRangeOperandsCanReferenceTheOuterRange) {
  constexpr std::string_view kInput = "{{#outer=1;3}}{{#inner=outer;outer}}{{inner}}{{/inner}}{{/outer}}";
  const Template tpl;
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "123");
}

TEST_F(MopeTest, RangeRejectsZeroAndNonNumericOperands) {
  constexpr std::string_view kZeroStepInput = "{{#index=1;3;0}}{{index}}{{/index}}";
  constexpr std::string_view kBadOperandInput = "{{#index=bad;3}}{{index}}{{/index}}";
  Template tpl;
  std::string zero_step(kZeroStepInput);
  EXPECT_THAT(tpl.Expand(zero_step).code(), absl::StatusCode::kInvalidArgument);

  ASSERT_THAT(tpl.SetValue("bad", "not-a-number"), absl::OkStatus());
  std::string bad_operand(kBadOperandInput);
  EXPECT_THAT(tpl.Expand(bad_operand).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(MopeTest, RangeRejectsSectionOperands) {
  constexpr std::string_view kInput = "{{#index=section;3}}{{index}}{{/index}}";
  Template tpl;
  ASSERT_THAT(tpl.AddSection("section").ok(), IsTrue());
  std::string output(kInput);
  EXPECT_THAT(tpl.Expand(output).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(MopeTest, RangeRejectsMissingOperandsAndDuplicateActiveNames) {
  const Template tpl;
  std::string missing("{{#index=missing;3}}{{index}}{{/index}}");
  EXPECT_THAT(tpl.Expand(missing).code(), absl::StatusCode::kNotFound);

  std::string duplicate("{{#index=1;1}}{{#index=1;1}}{{index}}{{/index}}{{/index}}");
  EXPECT_THAT(tpl.Expand(duplicate).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(MopeTest, RangeRejectsEachMissingOptionalOperand) {
  Template tpl;
  ASSERT_THAT(tpl.SetValue("one", "1"), absl::OkStatus());

  std::string missing_end("{{#index=one;missing}}{{index}}{{/index}}");
  EXPECT_THAT(tpl.Expand(missing_end).code(), absl::StatusCode::kNotFound);

  std::string missing_step("{{#index=one;2;missing}}{{index}}{{/index}}");
  EXPECT_THAT(tpl.Expand(missing_step).code(), absl::StatusCode::kNotFound);

  std::string missing_join("{{#index=one;2;1;missing}}{{index}}{{/index}}");
  EXPECT_THAT(tpl.Expand(missing_join).code(), absl::StatusCode::kNotFound);
}

TEST_F(MopeTest, ConfiguredListExpandsValuesAndAJoiner) {
  constexpr std::string_view kInput = R"({{#item=["one","two"];" / "}}{{item}}{{/item}})";
  const Template tpl;
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "one / two");
}

TEST_F(MopeTest, ConfiguredListLooksUpAJoinerValue) {
  constexpr std::string_view kInput = R"({{#item=["one","two"];join}}{{item}}{{/item}})";
  Template tpl;
  ASSERT_THAT(tpl.SetValue("join", " / "), absl::OkStatus());
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "one / two");
}

TEST_F(MopeTest, ConfiguredListRejectsASectionJoiner) {
  constexpr std::string_view kInput = R"({{#item=["one","two"];join}}{{item}}{{/item}})";
  Template tpl;
  ASSERT_THAT(tpl.AddSection("join").ok(), IsTrue());
  std::string output(kInput);
  EXPECT_THAT(tpl.Expand(output).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(MopeTest, ConfiguredListRejectsMalformedInputAndNameConflicts) {
  constexpr std::string_view kMalformedInput = "{{#item=[one,two}}{{item}}{{/item}}";
  constexpr std::string_view kConflictInput = "{{#item=[one,two]}}{{item}}{{/item}}";
  const Template tpl;
  std::string malformed(kMalformedInput);
  EXPECT_THAT(tpl.Expand(malformed).code(), absl::StatusCode::kInvalidArgument);

  Template conflicting;
  ASSERT_THAT(conflicting.SetValue("item", "existing"), absl::OkStatus());
  std::string conflict(kConflictInput);
  EXPECT_THAT(conflicting.Expand(conflict).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(MopeTest, ConfiguredListRejectsMalformedJoinersAndMissingReferences) {
  const Template tpl;
  std::string malformed("{{#item=[one,two]trailing}}{{item}}{{/item}}");
  EXPECT_THAT(tpl.Expand(malformed).code(), absl::StatusCode::kInvalidArgument);

  std::string malformed_literal(R"({{#item=[one,two];" / "trailing"}}{{item}}{{/item}})");
  EXPECT_THAT(tpl.Expand(malformed_literal).code(), absl::StatusCode::kInvalidArgument);

  std::string missing("{{#item=[one,two];missing}}{{item}}{{/item}}");
  EXPECT_THAT(tpl.Expand(missing).code(), absl::StatusCode::kNotFound);
}

TEST_F(MopeTest, ConfiguredListRejectsMalformedValuesAndNestedTemplates) {
  const Template tpl;
  std::string malformed_value(R"({{#item=["unterminated]}}{{item}}{{/item}})");
  EXPECT_THAT(tpl.Expand(malformed_value).code(), absl::StatusCode::kInvalidArgument);

  std::string nested_error("{{#item=[one]}}{{#bad}}{{/item}}");
  EXPECT_THAT(tpl.Expand(nested_error).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(MopeTest, EmptyAndUnknownSectionConfigurations) {
  constexpr std::string_view kEmptyInput = "before{{#item=}}content{{/item}}after";
  constexpr std::string_view kUnknownInput = "{{#item=unknown}}content{{/item}}";
  const Template tpl;
  std::string empty(kEmptyInput);
  ASSERT_THAT(tpl.Expand(empty), absl::OkStatus());
  EXPECT_THAT(empty, "beforeafter");

  std::string unknown(kUnknownInput);
  EXPECT_THAT(tpl.Expand(unknown).code(), absl::StatusCode::kUnimplemented);
}

TEST_F(MopeTest, ControlTagCannotOverrideATemplateValue) {
  constexpr std::string_view kInput = "{{name=replacement}}{{name}}";
  Template tpl;
  ASSERT_THAT(tpl.SetValue("name", "existing"), absl::OkStatus());
  std::string output(kInput);
  EXPECT_THAT(tpl.Expand(output).code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(MopeTest, ValueTagCannotRenderASectionDictionary) {
  Template tpl;
  ASSERT_THAT(tpl.AddSection("item").ok(), IsTrue());
  std::string output("{{item}}");
  EXPECT_THAT(tpl.Expand(output).code(), absl::StatusCode::kUnimplemented);
}

TEST_F(MopeTest, ReadIniToTemlateLoadsRootValues) {
  constexpr std::string_view kInput = "{{foo}}";
  const fs::path ini_path = fs::temp_directory_path() / "mope_test_root.ini";
  {
    std::ofstream out(ini_path);
    out << "foo=bar\n";
  }
  Template tpl;
  ASSERT_THAT(ReadIniToTemlate(ini_path.string(), tpl), absl::OkStatus());
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "bar");
  fs::remove(ini_path);
}

TEST_F(MopeTest, ReadIniToTemlateBuildsRepeatedNestedSections) {
  constexpr std::string_view kInput =
      R"({{root}}|{{#person:","}}{{name}}:{{#contact}}{{phone}}{{/contact}}{{/person}})";
  const fs::path ini_path = fs::temp_directory_path() / "mope_test_nested.ini";
  {
    std::ofstream out(ini_path);
    out << "root=top\n"
           "[person]\n"
           "name=one\n"
           "[person.contact]\n"
           "phone=123\n"
           "[person:second]\n"
           "name=two\n";
  }
  Template tpl;
  ASSERT_THAT(ReadIniToTemlate(ini_path.string(), tpl), absl::OkStatus());
  std::string output(kInput);
  ASSERT_THAT(tpl.Expand(output), absl::OkStatus());
  EXPECT_THAT(output, "top|one:123,two:");
  fs::remove(ini_path);
}

TEST_F(MopeTest, ReadIniToTemlateRejectsInvalidSectionNames) {
  const fs::path ini_path = fs::temp_directory_path() / "mope_test_invalid_section.ini";
  {
    std::ofstream out(ini_path);
    out << "[not valid]\nkey=value\n";
  }
  Template tpl;
  EXPECT_THAT(ReadIniToTemlate(ini_path.string(), tpl).code(), absl::StatusCode::kInvalidArgument);
  fs::remove(ini_path);
}

TEST_F(MopeTest, ReadIniToTemlateRejectsInvalidValueNames) {
  const fs::path ini_path = fs::temp_directory_path() / "mope_test_invalid_value.ini";
  {
    std::ofstream out(ini_path);
    out << "not valid=value\n";
  }
  Template tpl;
  EXPECT_THAT(ReadIniToTemlate(ini_path.string(), tpl).code(), absl::StatusCode::kInvalidArgument);
  fs::remove(ini_path);
}

TEST_F(MopeTest, ReadIniToTemlateFailsForAMissingFile) {
  Template tpl;
  EXPECT_THAT(ReadIniToTemlate("/nonexistent/no.ini", tpl).ok(), IsFalse());
}

}  // namespace
}  // namespace mbo::mope
