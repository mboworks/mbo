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

#include "mbo/mope/ini.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_split.h"
#include "mbo/file/ini/ini_file.h"
#include "mbo/mope/mope.h"
#include "mbo/status/status_macros.h"

namespace mbo::mope {

absl::Status ReadIniToTemplate(std::string_view ini_filename, Template& root_template) {
  using SectionPath = std::vector<std::pair<std::string, std::string>>;
  MBO_ASSIGN_OR_RETURN(const mbo::file::IniFile ini, mbo::file::IniFile::Read(ini_filename));
  absl::btree_map<SectionPath, mbo::mope::Template*> sections;
  // map insert: operator[] creates the entry, so there is no range to exceed.
  // NOLINTNEXTLINE(*-avoid-unchecked-container-access)
  sections[{{"", ""}}] = &root_template;
  for (const std::string& group : ini.GetGroups()) {
    mbo::mope::Template* target = &root_template;
    std::vector<std::string_view> levels = absl::StrSplit(group, '.');
    if (levels.size() == 1 && levels.front().empty()) {
      levels.clear();
    }
    if (!levels.empty()) {
      SectionPath section_path;
      for (const auto& level : levels) {
        const std::pair<std::string_view, std::string_view> tail = absl::StrSplit(level, absl::MaxSplits(':', 1));
        section_path.emplace_back(tail.first, tail.second);
        auto [section, inserted] = sections.emplace(section_path, nullptr);
        if (inserted) {
          MBO_ASSIGN_OR_RETURN(section->second, target->AddSection(tail.first));
        }
        target = section->second;
      }
    }
    for (const auto& [key, val] : ini.GetGroupData(group)) {
      MBO_RETURN_IF_ERROR(target->SetValue(key, val));
    }
  }
  return absl::OkStatus();
}

absl::Status ReadIniToTemlate(std::string_view ini_filename, Template& root_template) {
  return ReadIniToTemplate(ini_filename, root_template);
}

}  // namespace mbo::mope
