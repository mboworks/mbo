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

#include "mbo/diff/diff_options.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/absl_check.h"
#include "absl/strings/str_split.h"
#include "mbo/container/limited_map.h"
#include "mbo/types/no_destruct.h"
#include "re2/re2.h"

namespace mbo::diff {

std::optional<DiffOptions::Algorithm> DiffOptions::ParseAlgorithmFlag(std::string_view flag) {
  constexpr auto kFlagMapping = mbo::container::ToLimitedMap<std::string_view, DiffOptions::Algorithm>({
      {"direct", DiffOptions::Algorithm::kDirect},
      {"myers", DiffOptions::Algorithm::kMyers},
      {"naive", DiffOptions::Algorithm::kNaive},
      {"unified", DiffOptions::Algorithm::kMyers},  // Deprecated alias; implies unified format.
  });
  const auto it = kFlagMapping.find(flag);
  if (it == kFlagMapping.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<DiffOptions::OutputFormat> DiffOptions::ParseOutputFormatFlag(std::string_view flag) {
  constexpr auto kFlagMapping = mbo::container::ToLimitedMap<std::string_view, DiffOptions::OutputFormat>({
      {"context", DiffOptions::OutputFormat::kContext},
      {"normal", DiffOptions::OutputFormat::kNormal},
      {"side-by-side", DiffOptions::OutputFormat::kSideBySide},
      {"unified", DiffOptions::OutputFormat::kUnified},
  });
  const auto it = kFlagMapping.find(flag);
  if (it == kFlagMapping.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<DiffOptions::FileHeaderUse> DiffOptions::ParseFileHeaderUse(std::string_view flag) {
  constexpr auto kFlagMapping = mbo::container::ToLimitedMap<std::string_view, DiffOptions::FileHeaderUse>({
      {"both", DiffOptions::FileHeaderUse::kBoth},
      {"left", DiffOptions::FileHeaderUse::kLeft},
      {"none", DiffOptions::FileHeaderUse::kNone},
      {"right", DiffOptions::FileHeaderUse::kRight},
  });
  const auto it = kFlagMapping.find(flag);
  if (it == kFlagMapping.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<DiffOptions::RegexReplace> DiffOptions::ParseRegexReplaceFlag(std::string_view flag) {
  if (flag.empty()) {
    return std::nullopt;
  }
  const char separator = flag.front();
  std::vector<std::string> parts = absl::StrSplit(flag, separator);
  ABSL_CHECK_EQ(parts.size(), 4U);
  ABSL_CHECK(parts.at(0).empty());
  ABSL_CHECK(parts.at(3).empty());
  return DiffOptions::RegexReplace{
      .regex = std::make_unique<RE2>(parts.at(1)),
      .replace{parts.at(2)},
  };
}

const DiffOptions& DiffOptions::Default() noexcept {
  static const mbo::types::NoDestruct<DiffOptions> kDefaults;
  return *kDefaults;
}

}  // namespace mbo::diff
