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

#include "mbo/file/ini/ini_file.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/log/absl_log.h"  // IWYU pragma: keep
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "mbo/file/file.h"
#include "mbo/status/status_macros.h"

namespace mbo::file {

absl::StatusOr<IniFile> IniFile::Read(std::string_view filename) {
  // TODO(helly25): Implement LineReader?
  MBO_ASSIGN_OR_RETURN(const std::string content, GetContents(filename));
  return ParseStrict(content);
}

absl::StatusOr<IniFile> IniFile::ReadPermissive(std::string_view filename) {
  MBO_ASSIGN_OR_RETURN(const std::string content, GetContents(filename));
  return ParsePermissive(content);
}

absl::StatusOr<IniFile> IniFile::ParseStrict(std::string_view content) {
  const std::vector<std::string_view> lines = absl::StrSplit(content, '\n');
  IniFile ini;
  std::string_view group;
  absl::btree_map<std::pair<std::string, std::string>, std::size_t> first_key_lines;
  std::size_t line_number = 0;
  for (const std::string_view raw_line : lines) {
    ++line_number;
    std::string_view line = absl::StripAsciiWhitespace(raw_line);
    if (line.empty() || line.starts_with(';') || line.starts_with('#')) {
      continue;
    }
    if (line.starts_with('[')) {
      if (!line.ends_with(']')) {
        return absl::InvalidArgumentError(absl::StrCat("line ", line_number, ": malformed group header"));
      }
      line.remove_prefix(1);
      line.remove_suffix(1);
      group = absl::StripAsciiWhitespace(line);
      continue;
    }
    if (line.ends_with(']')) {
      return absl::InvalidArgumentError(absl::StrCat("line ", line_number, ": malformed group header"));
    }
    const std::size_t separator = line.find('=');
    if (separator == std::string_view::npos) {
      return absl::InvalidArgumentError(absl::StrCat("line ", line_number, ": expected key=value"));
    }
    const std::string_view key = absl::StripAsciiWhitespace(line.substr(0, separator));
    const std::string_view value = absl::StripAsciiWhitespace(line.substr(separator + 1));
    if (key.empty()) {
      return absl::InvalidArgumentError(absl::StrCat("line ", line_number, ": key must not be empty"));
    }
    const auto [it, inserted] = first_key_lines.emplace(std::pair<std::string, std::string>(group, key), line_number);
    if (!inserted) {
      return absl::InvalidArgumentError(
          absl::StrCat(
              "line ", line_number, ": duplicate key '", key, "' in group [", group, "]; first defined on line ",
              it->second));
    }
    ini.SetKey({.group = group, .key = key}, std::string(value));
  }
  return ini;
}

IniFile IniFile::Parse(std::string_view content) {
  return ParsePermissive(content);
}

IniFile IniFile::ParsePermissive(std::string_view content) {
  const std::vector<std::string_view> lines = absl::StrSplit(content, '\n', absl::SkipEmpty());
  IniFile ini;
  std::string_view group;
  for (auto line : lines) {
    line = absl::StripAsciiWhitespace(line);
    if (line.starts_with('[') && line.ends_with(']')) {
      line.remove_prefix(1);
      line.remove_suffix(1);
      group = line;
      continue;
    }
    if (line.starts_with(';') || line.starts_with('#')) {
      continue;
    }
    const std::pair<std::string_view, std::string_view> key_val = absl::StrSplit(line, absl::MaxSplits('=', 1));
    auto [key, val] = key_val;
    key = absl::StripAsciiWhitespace(key);
    val = absl::StripAsciiWhitespace(val);
    ini.SetKey({.group = group, .key = key}, val);
  }
  return ini;
}

absl::StatusOr<std::string> IniFile::GetKeyOrStatus(const GroupKey& group_key) const {
  const auto [group, key] = Clean(group_key);
  const auto group_it = data_.find(group);
  if (group_it == data_.end()) {
    return absl::NotFoundError(absl::StrCat("Group [", group, "] not found."));
  }
  const auto value_it = group_it->second.find(key);
  if (value_it == group_it->second.end()) {
    return absl::NotFoundError(absl::StrCat("Group [", group, "] has no key '", key, "'."));
  }
  return value_it->second;
}

absl::Status IniFile::Write(std::string_view filename) const {
  std::string content;
  for (const auto& [group, key_val] : data_) {
    if (!group.empty()) {
      if (!content.empty()) {
        absl::StrAppend(&content, "\n");
      }
      absl::StrAppend(&content, "[", group, "]\n");
    }
    for (const auto& [key, val] : key_val) {
      absl::StrAppend(&content, key, "=", val, "\n");
    }
  }
  return SetContents(filename, content);
}

std::size_t IniFile::size() const {
  std::size_t size = 0;
  for (const auto& [unused, kvs] : data_) {
    size += kvs.size();
  }
  return size;
}

}  // namespace mbo::file
