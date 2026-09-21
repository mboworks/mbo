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
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "mbo/diff/diff.h"
#include "mbo/file/artefact.h"
#include "mbo/strings/strip.h"
#include "re2/re2.h"

namespace {

using Algorithm = mbo::diff::DiffOptions::Algorithm;
using FileHeaderUse = mbo::diff::DiffOptions::FileHeaderUse;
using OutputFormat = mbo::diff::DiffOptions::OutputFormat;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  constexpr std::size_t kControlSize = 8;
  if (size < kControlSize) {
    return 0;
  }

  // libFuzzer exposes bytes as uint8_t; a char view preserves the same object representation.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const std::string_view payload(reinterpret_cast<const char*>(data + kControlSize), size - kControlSize);
  const std::size_t part_size = payload.size() / 4;
  const std::string_view pattern = payload.substr(0 * part_size, part_size);
  const std::string_view replacement = payload.substr(1 * part_size, part_size);
  const std::string_view lhs = payload.substr(2 * part_size, part_size);
  const std::string_view rhs = payload.substr(3 * part_size);

  mbo::diff::DiffOptions options{
      .algorithm = static_cast<Algorithm>(data[0] % 3U),
      .output_format = static_cast<OutputFormat>(data[1] % 4U),
      .context_size = data[2] % 16U,
      .side_by_side_width = 3U + (data[3] % 253U),
      .file_header_use = static_cast<FileHeaderUse>(data[4] % 4U),
      .ignore_blank_lines = (data[5] & 0x01U) != 0,
      .ignore_case = (data[5] & 0x02U) != 0,
      .ignore_matching_chunks = (data[5] & 0x04U) != 0,
      .ignore_all_space = (data[5] & 0x08U) != 0,
      .ignore_consecutive_space = (data[5] & 0x10U) != 0,
      .ignore_trailing_space = (data[5] & 0x20U) != 0,
      .ignore_missing_final_newline = (data[5] & 0x40U) != 0,
      .minimal = (data[5] & 0x80U) != 0,
      .show_chunk_headers = (data[6] & 0x01U) != 0,
      .skip_left_deletions = (data[6] & 0x02U) != 0,
      .strip_file_header_prefix = std::string(replacement),
      .max_diff_chunk_length = 1U + (data[7] % 64U),
      .time_format = "",
  };

  if ((data[6] & 0x04U) != 0) {
    options.ignore_matching_lines.emplace(std::string(pattern));
  }
  if ((data[6] & 0x08U) != 0) {
    options.regex_replace_lhs.emplace(
        mbo::diff::DiffOptions::RegexReplace{
            .regex = std::make_unique<RE2>(std::string(pattern)),
            .replace = std::string(replacement),
        });
  }
  if ((data[6] & 0x10U) != 0) {
    options.regex_replace_rhs.emplace(
        mbo::diff::DiffOptions::RegexReplace{
            .regex = std::make_unique<RE2>(std::string(pattern)),
            .replace = std::string(replacement),
        });
  }
  switch ((data[6] >> 5U) % 3U) {
    default: break;
    case 1:
      options.strip_comments = mbo::strings::StripCommentArgs{
          .comment_start = pattern,
          .strip_trailing_whitespace = (data[7] & 0x80U) != 0,
      };
      break;
    case 2:
      options.strip_comments = mbo::strings::StripParsedCommentArgs{
          .parse = {.stop_at_any_of = pattern},
          .strip_trailing_whitespace = (data[7] & 0x80U) != 0,
      };
      break;
  }

  (void)mbo::diff::Diff::FileDiff(
      {.data = std::string(lhs), .name = "lhs"}, {.data = std::string(rhs), .name = "rhs"}, options);
  return 0;
}
