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

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "mbo/container/any_scan.h"
#include "mbo/status/status_macros.h"
#include "mbo/strings/parse.h"
#include "re2/re2.h"

namespace mbo::mope {
namespace {

// NOLINTBEGIN(misc-no-recursion)

std::pair<std::size_t, std::size_t> ExpandWhiteSpace(
    std::string_view output,
    std::size_t tag_pos,
    std::size_t tag_len) {
  std::size_t end = tag_pos + tag_len;
  // Is the tag followed by a new-line?
  if (end < output.size() && (output.at(end) == '\n' || output.at(end) == '\r')) {
    ++end;
    std::size_t pos = tag_pos;
    // reverse past all space
    while (pos > 0 && (output.at(pos - 1) == ' ' || output.at(pos - 1) == '\t')) {
      --pos;
    }
    if (pos == 0 || output.at(pos - 1) == '\n' || output.at(pos - 1) == '\r') {
      return {pos, tag_pos - pos + tag_len + 1};
    }
  }
  return {tag_pos, tag_len};
}

}  // namespace

// LCOV_EXCL_FUNC_LINE: Only instantiated by fatal ABSL_CHECK_EQ diagnostics.
template<typename Sink>
void AbslStringify(Sink& sink, Template::TagType value) {
  // LCOV_EXCL_FUNC_LINE: Fatal subprocess profiles cannot flush.
  absl::Format(&sink, "%s", [value] {
    switch (value) {
      case Template::TagType::kValue: return "TagType::Value";
      case Template::TagType::kSection: return "TagType::Section";
      case Template::TagType::kControl: return "TagType::Control";
    }
    return "TagType::UNKNOWN";
  }());
}

bool Template::IsValidName(std::string_view name) {
  static constexpr LazyRE2 kValid = {.pattern_ = "[_a-zA-Z]\\w*"};
  return RE2::FullMatch(name, *kValid);
}

absl::StatusOr<Template*> Template::AddSection(std::string_view name) {
  if (!IsValidName(name)) {
    return absl::InvalidArgumentError(absl::StrCat("Name '", name, "' is not valid."));
  }
  TagInfo tag{
      .name = std::string(name),
      .start = absl::StrCat("{{#", name, "}}"),
      .end = absl::StrCat("{{/", name, "}}"),
      .type = TagType::kSection,
  };
  auto it = data_.emplace(name, TagData<Section>{.tag = std::move(tag), .data = {}}).first;
  auto* section = std::get_if<TagData<Section>>(&it->second);
  if (section == nullptr) {
    return absl::AlreadyExistsError(absl::StrCat("A value for '", name, "' already exists with a different type."));
  }
  return &section->data.dictionary.emplace_back();
}

absl::Status Template::SetValueInternal(
    std::string_view name,
    std::string_view value,
    bool allow_update,
    DataMap& data) {
  if (!IsValidName(name)) {
    return absl::InvalidArgumentError(absl::StrCat("Name '", name, "' is not valid."));
  }
  TagInfo tag{
      .name = std::string(name),
      .start = absl::StrCat("{{", name, "}}"),
      .type = TagType::kValue,
  };
  auto [it, inserted] = data.emplace(name, TagData<std::string>{.tag = std::move(tag), .data = std::string(value)});
  if (inserted) {
    return absl::OkStatus();
  }
  if (allow_update) {
    auto* data_str = std::get_if<TagData<std::string>>(&it->second);
    if (data_str == nullptr) {
      return absl::AlreadyExistsError(absl::StrCat("A value for '", name, "' already exists with a different type."));
    }
    data_str->data.assign(value);
    return absl::OkStatus();
  }
  return absl::AlreadyExistsError(absl::StrCat("A value for '", name, "' already exists."));
}

absl::Status Template::SetValue(std::string_view name, std::string_view value, bool allow_update) {
  return SetValueInternal(name, value, allow_update, data_);
}

bool Template::Exists(std::string_view name, const Context& ctx) const {
  return data_.contains(name) || ctx.data.contains(name);
}

const Template::Data* Template::Lookup(std::string_view name, const Context& ctx) const {
  auto data_it = data_.find(name);
  if (data_it != data_.end()) {
    return &data_it->second;
  }
  data_it = ctx.data.find(name);
  if (data_it != ctx.data.end()) {
    return &data_it->second;
  }
  return nullptr;
}

absl::Status Template::MaybeLookup(
    const TagInfo& tag_info,
    std::string_view data,
    const Context& ctx,
    std::string& value) const {
  if (data.empty()) {
    return absl::OkStatus();
  }
  if ((data.starts_with('"') || data.starts_with('\'')) && (data.ends_with('"') || data.ends_with('\''))) {
    static constexpr mbo::strings::ParseOptions kOptions{
        .remove_quotes = true,
        .allow_unquoted = false,
    };
    MBO_ASSIGN_OR_RETURN(value, ParseString(kOptions, data));
    if (!data.empty()) {
      return absl::InvalidArgumentError(absl::StrCat("Tag '", tag_info.name, "' has bad literal joiner '", data, "'."));
    }
    return absl::OkStatus();
  }
  const Data* data_found = Lookup(data, ctx);
  if (data_found == nullptr) {
    return absl::NotFoundError(absl::StrCat("Tag '", tag_info.name, "' references '", data, "' which was not found."));
  }
  if (const auto* tag_data = std::get_if<TagData<std::string>>(data_found)) {
    value = tag_data->data;
    return absl::OkStatus();
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Tag '", tag_info.name, "' references '", data, "' which has unsupported data type."));
}

absl::Status Template::MaybeLookup(const TagInfo& tag_info, std::string_view data, const Context& ctx, int& value)
    const {
  if (data.empty() || absl::SimpleAtoi(data, &value)) {
    return absl::OkStatus();
  }
  const Data* found_data_ptr = Lookup(data, ctx);
  if (found_data_ptr == nullptr) {
    return absl::NotFoundError(absl::StrCat("Tag '", tag_info.name, "' references '", data, "' which was not found."));
  }
  if (const auto* tag_data = std::get_if<TagData<std::string>>(found_data_ptr)) {
    if (absl::SimpleAtoi(tag_data->data, &value)) {
      return absl::OkStatus();
    }
    return absl::InvalidArgumentError(
        absl::StrCat(
            "Tag '", tag_info.name, "' references '", data, "' which has non numeric value '", tag_data->data, "'"));
  } else if (const auto* tag_data = std::get_if<TagData<Range>>(found_data_ptr)) {
    // Range entries exist in a Context only while ExpandRangeTag is active.
    value = tag_data->data.curr;
    return absl::OkStatus();
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Tag '", tag_info.name, "' references '", data, "' which has unsupported data type."));
}

absl::Status Template::ExpandRangeTag(const TagInfo& /*tag*/, Range& range, Context& ctx, std::string& output) const {
  const std::string original = std::move(output);
  output = "";
  if (range.step == 0) {
    return absl::InternalError(absl::StrCat("A range should never have step == 0."));
  }
  range.expanding = true;
  const absl::Cleanup cleanup = [&range] { range.expanding = false; };
  for (range.curr = range.start; range.step > 0 ? range.curr <= range.end : range.curr >= range.end;
       range.curr += range.step) {
    if (!range.join.empty() && range.curr != range.start) {
      output.append(range.join);
    }
    std::string expanded = original;
    // It is possible to use `StrReplaceAll` here, but `Expand` has to be called anyway, so it can do the replacement.
    // absl::StrReplaceAll({{absl::StrCat("{{", tag.name, "}}"), absl::StrCat(range.curr)}}, &expanded);
    MBO_RETURN_IF_ERROR(ExpandInternal(ctx, expanded));
    output.append(expanded);
  }
  return absl::OkStatus();
}

absl::Status Template::ExpandRangeData(
    const TagInfo& tag,
    const RangeData& range_data,
    Context& ctx,
    std::string& output) const {
  Range range;
  MBO_RETURN_IF_ERROR(MaybeLookup(tag, range_data.start, ctx, range.start));
  MBO_RETURN_IF_ERROR(MaybeLookup(tag, range_data.end, ctx, range.end));
  MBO_RETURN_IF_ERROR(MaybeLookup(tag, range_data.step, ctx, range.step));
  MBO_RETURN_IF_ERROR(MaybeLookup(tag, range_data.join, ctx, range.join));
  if (range.step == 0) {
    return absl::InvalidArgumentError(absl::StrCat("Tag '", tag.name, "' cannot have step == 0."));
  }
  // We could check:
  // * range.step > 0 && range.start > range.end
  // * range.step < 0 && range.start < range.end
  // But that would not work well with values that were looked up or computed.
  auto [r_tag_it, inserted] = ctx.data.emplace(tag.name, TagData<Range>{.tag = tag, .data = range});
  if (!inserted) {
    return absl::InvalidArgumentError(absl::StrCat("Tag '", tag.name, "' appears twice."));
  }
  auto& tag_range = std::get<TagData<Range>>(r_tag_it->second).data;
  auto result = ExpandRangeTag(tag, tag_range, ctx, output);
  ctx.data.erase(tag.name);
  return result;
}

absl::Status Template::ExpandSection(const TagData<Section>& tag, Context& ctx, std::string& output) {
  const std::string original(std::move(output));
  output = "";
  bool first = true;
  for (const Template& section : tag.data.dictionary) {
    std::string result(original);
    MBO_RETURN_IF_ERROR(section.ExpandInternal(ctx, result));
    if (!first) {
      output.append(tag.data.join);
    }
    first = false;
    output.append(result);
  }
  return absl::OkStatus();
}

absl::Status Template::ExpandConfiguredSection(
    std::string_view name,
    std::vector<std::string> str_list,  // NOLINT(performance-unnecessary-value-param): Incorrect, we move strings out.
    std::string_view join,
    Context& ctx,
    std::string& output) const {
  const std::string original(std::move(output));
  output = "";
  bool first = true;
  if (Exists(name, ctx)) {
    return absl::InvalidArgumentError(absl::StrCat("Cannot override existing section tag '", name, "'."));
  }
  const absl::Cleanup cleanup = [&] { ctx.data.erase(name); };
  for (auto& str : str_list) {
    MBO_RETURN_IF_ERROR(SetValueInternal(name, std::move(str), true, ctx.data));
    std::string result(original);
    MBO_RETURN_IF_ERROR(ExpandInternal(ctx, result));
    if (!first) {
      output.append(join);
    }
    first = false;
    output.append(result);
  }
  return absl::OkStatus();
}

absl::Status Template::ExpandConfiguredList(
    const TagInfo& tag,
    std::string_view str_list_data,
    Context& ctx,
    std::string& output) const {
  static constexpr mbo::strings::ParseOptions kConfiguredListParseOptions{
      .stop_at_any_of = "]",
      .split_at_any_of = ",",
  };
  str_list_data.remove_prefix(1);  // Drop '['
  // str_list_data.remove_suffix(1);  // Drop ']'
  MBO_ASSIGN_OR_RETURN(auto str_list, mbo::strings::ParseStringList(kConfiguredListParseOptions, str_list_data));
  std::string join;
  if (str_list_data.empty() || mbo::strings::PopChar(str_list_data) != ']') {
    return absl::InvalidArgumentError(
        absl::StrCat("Tag '", tag.name, "' has unknown config format '", tag.config.value_or(""), "'."));
  }
  if (!str_list_data.empty()) {
    if (str_list_data.size() < 2 || mbo::strings::PopChar(str_list_data) != ';') {
      return absl::InvalidArgumentError(
          absl::StrCat("Tag '", tag.name, "' has unknown config format '", tag.config.value_or(""), "'."));
    }
    MBO_RETURN_IF_ERROR(MaybeLookup(tag, str_list_data, ctx, join));
  }
  if (Exists(tag.name, ctx)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Tag '", tag.name, "' may not be present prior to expanding a list of the same name."));
  }
  // CONSIDER: A specialized type would make this faster. But also less generic
  // and thus complicate extensions.
  MBO_RETURN_IF_ERROR(ExpandConfiguredSection(tag.name, std::move(str_list), std::move(join), ctx, output));
  return absl::OkStatus();
}

absl::Status Template::ExpandSectionTag(const TagInfo& tag, Context& ctx, std::string& output) const {
  // Parse tag config and translate into whatever that says...
  ABSL_CHECK_EQ(tag.type, TagType::kSection);
  if (!tag.config.has_value()) {
    const Data* found_data_ptr = Lookup(tag.name, ctx);
    if (found_data_ptr == nullptr) {
      output.clear();
      return absl::OkStatus();
    }
    const auto* section = std::get_if<TagData<Section>>(found_data_ptr);
    if (section == nullptr) {
      return absl::InvalidArgumentError(absl::StrCat("Section tag '", tag.name, "' has no dictionary."));
    }
    if (section->data.dictionary.empty()) {
      output.clear();
      return absl::OkStatus();
    }
    static constexpr mbo::strings::ParseOptions kOptions{
        .remove_quotes = true,
        .allow_unquoted = false,
    };
    std::string_view join;
    if (tag.option.has_value()) {
      join = *tag.option;
    }
    MBO_ASSIGN_OR_RETURN(section->data.join, ParseString(kOptions, join));
    return ExpandSection(*section, ctx, output);
  }
  static constexpr LazyRE2 kReFor = {
      .pattern_ =
          R"(\s*(-?\d+|[_a-zA-Z]\w*)\s*;\s*(-?\d+|[_a-zA-Z]\w*)\s*(?:;\s*(|-?\d+|[_a-zA-Z]\w*)\s*(?:;([^;]*))?)?)"};
  RangeData range;
  if (RE2::FullMatch(*tag.config, *kReFor, &range.start, &range.end, &range.step, &range.join)) {
    return ExpandRangeData(tag, range, ctx, output);
  }
  if (tag.config->empty()) {
    output = "";
    return absl::OkStatus();
  }
  if (!tag.config->starts_with('[')) {
    return absl::UnimplementedError(
        absl::StrCat("Tag '", tag.name, "' has unknown config format '", *tag.config, "'."));
  }
  return ExpandConfiguredList(tag, *tag.config, ctx, output);
}

absl::Status Template::ExpandControlTag(const TagInfo& tag, Context& ctx) const {
  ABSL_CHECK_EQ(tag.type, TagType::kControl);
  if (!tag.config.has_value()) {
    return absl::OkStatus();
  }
  if (data_.contains(tag.name)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Control tag '", tag.name, "' cannot override an existing template tag."));
  }
  return SetValueInternal(tag.name, *tag.config, true, ctx.data);
}

absl::StatusOr<bool> Template::ExpandValueTag(const TagInfo& tag, Context& ctx, std::string& output) const {
  ABSL_CHECK_EQ(tag.type, TagType::kValue);
  const Data* found_data_ptr = Lookup(tag.name, ctx);
  if (found_data_ptr == nullptr) {
    return false;
  }
  const auto* tag_str = std::get_if<TagData<std::string>>(found_data_ptr);
  if (tag_str != nullptr) {
    output = tag_str->data;
    return true;
  }
  const auto* tag_range = std::get_if<TagData<Range>>(found_data_ptr);
  if (tag_range != nullptr) {
    output = absl::StrCat(tag_range->data.curr);
    return true;
  }
  return absl::UnimplementedError(absl::StrCat("Tag '", tag.name, "' cannot be handled."));
}

std::optional<const Template::TagInfo> Template::FindAndConsumeTag(std::string_view& pos) {
  // Grab parts as string_view. Done in a separate block so these cannot escape.
  // They will be invalid upon the first change of `output`.
  static constexpr LazyRE2 kTagRegex = {.pattern_ = "({{(#?)([_a-zA-Z]\\w*)(?:([=:])((?:[^}]|[}][^}])*))?}})"};
  std::string_view start;
  std::string_view type;
  std::string_view name;
  std::string_view has_extra;
  std::string_view extra;
  if (!RE2::FindAndConsume(&pos, *kTagRegex, &start, &type, &name, &has_extra, &extra)) {
    return std::nullopt;
  }
  return TagInfo{
      .name = std::string(name),
      .start = std::string(start),
      .end = type.empty() ? "" : absl::StrCat("{{/", name, "}}"),
      .config = has_extra == "=" ? std::optional<std::string>(extra) : std::nullopt,
      .option = has_extra == ":" ? std::optional<std::string>(extra) : std::nullopt,
      .type = type.empty() ? (extra.empty() ? TagType::kValue : TagType::kControl) : TagType::kSection,
  };
}

std::pair<std::size_t, std::size_t> Template::MaybeExpandWhiteSpace(
    std::string_view output,
    const TagInfo& tag,
    std::size_t tag_pos) {
  return tag.type == TagType::kSection || tag.config.has_value() ? ExpandWhiteSpace(output, tag_pos, tag.start.length())
                                                                 : std::make_pair(tag_pos, tag.start.length());
}

// A single scan over the output that dispatches on each tag kind it meets; at 32 it is barely over
// the threshold, and the tag handling reads as one sequence rather than as separable steps.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
absl::Status Template::ExpandInternal(Context& ctx, std::string& output) const {
  std::string_view pos = output;
  while (true) {
    if (pos.empty()) {
      return absl::OkStatus();
    }
    std::optional<TagInfo> result = FindAndConsumeTag(pos);
    if (result == std::nullopt) {
      return absl::OkStatus();
    }
    const TagInfo tag = *std::move(result);
    const std::size_t tag_pos = pos.data() - output.c_str() - tag.start.length();
    const auto [replace_pos, replace_tag_len] = MaybeExpandWhiteSpace(output, tag, tag_pos);
    std::string replace_str;
    std::size_t replace_len = 0;
    switch (tag.type) {
      case TagType::kControl: {
        replace_len = replace_tag_len;
        MBO_RETURN_IF_ERROR(ExpandControlTag(tag, ctx));
        break;
      }
      case TagType::kSection: {
        const auto tag_end_pos = output.find(tag.end, replace_pos + replace_tag_len);
        if (tag_end_pos == std::string_view::npos) {
          return absl::InvalidArgumentError(absl::StrCat("Tag name '", tag.name, "' has no end tag '", tag.end, "'."));
        }
        const auto [replace_end, replace_end_len] = ExpandWhiteSpace(output, tag_end_pos, tag.end.length());
        replace_len = replace_end + replace_end_len - replace_pos;  // whole replace incl. tags
        replace_str = output.substr(replace_pos + replace_tag_len, replace_len - replace_tag_len - replace_end_len);
        MBO_RETURN_IF_ERROR(ExpandSectionTag(tag, ctx, replace_str));
        break;
      }
      case TagType::kValue: {
        MBO_ASSIGN_OR_RETURN(const bool replace, ExpandValueTag(tag, ctx, replace_str));
        if (replace) {
          replace_len = replace_tag_len;
          break;
        }
        pos = output;
        pos.remove_prefix(replace_pos + replace_tag_len);
        continue;
      }
    }
    output.erase(replace_pos, replace_len);
    output.insert(replace_pos, replace_str);
    pos = output;
    pos.remove_prefix(replace_pos + replace_str.length());
  }
}

absl::Status Template::Expand(std::string& output) const {
  Context ctx;
  return ExpandInternal(ctx, output);
}

absl::Status Template::Expand(
    std::string& output,
    const mbo::container::ConvertingScan<std::pair<std::string_view, std::string_view>>& context_data) const {
  Context ctx;
  for (auto [name, value] : context_data) {
    MBO_RETURN_IF_ERROR(SetValueInternal(name, value, false, ctx.data));
  }
  return ExpandInternal(ctx, output);
}

// NOLINTEND(misc-no-recursion)

}  // namespace mbo::mope
