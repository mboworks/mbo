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

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl//container/btree_map.h"
#include "absl//container/btree_set.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "mbo/file/glob.h"
#include "mbo/status/status.h"
#include "mbo/strings/numbers.h"
#include "mbo/types/extend.h"

// NOLINTBEGIN: Bad flags macros

ABSL_FLAG(bool, depth, false, "Whether to show the directory depth (0 = provided root).");
ABSL_FLAG(bool, dotdir, true, "Whether to allow or skip directories starting with a '.'.");
ABSL_FLAG(bool, dotfile, true, "Whether to allow or skip files starting with a '.'.");
ABSL_FLAG(bool, entries, true, "Whether to show entries.");
ABSL_FLAG(bool, fast, false, "Whether to show entries fast (no buffering and no field alignment).");
ABSL_FLAG(
    bool,
    re2,
    false,
    "Whether to use re2 regular expressions. This program uses Google's Re2: "
    "https://github.com/google/re2/wiki/Syntax.");
ABSL_FLAG(bool, recurse_match, true, "Whether to recurse into matches.");
ABSL_FLAG(bool, size, false, "Whether to show file sizes.");
ABSL_FLAG(bool, sum, false, "Whether to show summary. This is automatically enabled if --sum_every > 0.");
ABSL_FLAG(std::size_t, sum_every, 0, "If greater zero, then show a summary after every X entry.");
ABSL_FLAG(bool, sum_extensions, false, "Whether to show an extension summary.");
ABSL_FLAG(bool, type, false, "Whether to show the file type");
// NOLINTEND

using mbo::strings::BigNumber;
using mbo::strings::BigNumberLen;

namespace {

class Entries {
 public:
  struct Entry : mbo::types::Extend<Entry> {
    mbo::file::GlobEntry glob_entry;
  };

  ~Entries() = default;

  explicit Entries(std::filesystem::path root)
      : root_(std::move(root)),
        dotdir_(absl::GetFlag(FLAGS_dotdir)),
        dotfile_(absl::GetFlag(FLAGS_dotfile)),
        show_fast_(absl::GetFlag(FLAGS_fast)),
        show_size_(absl::GetFlag(FLAGS_size)),
        show_type_(absl::GetFlag(FLAGS_type)),
        show_depth_(absl::GetFlag(FLAGS_depth)),
        sum_extensions_(absl::GetFlag(FLAGS_sum_extensions)),
        sum_every_(absl::GetFlag(FLAGS_sum_every)),
        stats_{
            {"Dirs", dirs_},          {"FileSize", size_}, {"Files", files_}, {"Links", links_},
            {"MaxDepth", depth_max_}, {"Other", other_},   {"Total", seen_},
        } {}

  Entries(const Entries&) = delete;
  Entries& operator=(const Entries&) = delete;
  Entries(Entries&&) noexcept = default;
  Entries& operator=(Entries&&) = delete;

  mbo::file::GlobEntryAction Add(const mbo::file::GlobEntry& glob_entry) {
    // Filter
    if (glob_entry.entry.is_directory()) {
      if (!dotdir_ && glob_entry.entry.path().filename().string().starts_with('.')) {
        return mbo::file::GlobEntryAction::kDoNotRecurse;
      }
    } else if (
        glob_entry.entry.is_regular_file() && !dotfile_
        && glob_entry.entry.path().filename().string().starts_with('.')) {
      return mbo::file::GlobEntryAction::kContinue;
    }
    // Actual Add
    if (glob_entry.entry.is_directory()) {
      ++dirs_;
    } else if (glob_entry.entry.is_symlink()) {
      ++links_;
    } else if (glob_entry.entry.is_regular_file()) {
      ++files_;
      const std::size_t size = glob_entry.FileSize();
      size_ += size;
      size_max_ = std::max(size_max_, size);
      if (sum_extensions_) {
        // NOLINTBEGIN(*-avoid-unchecked-container-access)
        // map insert: operator[] creates the entry, so there is no range to exceed.
        if (glob_entry.entry.path().has_extension()) {
          const auto ext = glob_entry.entry.path().extension().string();
          ++extensions_[absl::StrCat("FileExt(", ext, ")")];
          extensions_[absl::StrCat("FileSize(", ext, ")")] += size;
        } else if (glob_entry.entry.path().filename().string().starts_with('.')) {
          // Dot files (/.conf) are shown as the full file name.
          const auto ext = glob_entry.entry.path().filename().string();
          ++extensions_[absl::StrCat("FileExt(", ext, ")")];
          extensions_[absl::StrCat("FileSize(", ext, ")")] += size;
        } else {
          ++extensions_["FileExt()"];
          extensions_["FileSize()"] += size;
        }
        // NOLINTEND(*-avoid-unchecked-container-access)
      }
    } else {
      ++other_;
    }
    depth_max_ = std::max(depth_max_, static_cast<std::size_t>(glob_entry.depth));
    ++seen_;
    Entry entry{
        .glob_entry = glob_entry,
    };
    // Maybe Print
    if (show_fast_) {
      PrintFastEntry(entry);
    } else {
      entries_.emplace(entry);
    }
    if (sum_every_ > 0 && seen_ % sum_every_ == 0) {
      std::cout << "\n";
      PrintSummary();
    }
    return absl::GetFlag(FLAGS_recurse_match)  // NL
               ? mbo::file::GlobEntryAction::kContinue
               : mbo::file::GlobEntryAction::kDoNotRecurse;
  }

  static char GetType(const std::filesystem::directory_entry& entry) {
    if (entry.is_directory()) {
      return 'd';
    } else if (entry.is_symlink()) {
      return 'l';
    } else if (entry.is_regular_file()) {
      return 'f';
    } else if (entry.is_block_file()) {
      return 'b';
    } else if (entry.is_character_file()) {
      return 'c';
    } else if (entry.is_fifo()) {
      return 'p';
    } else if (entry.is_socket()) {
      return 's';
    }
    // NO 'o' for other: entry.is_other()) {
    return '?';
  }

  void PrintEntry(const Entry& entry) const {
    if (show_size_) {
      std::cout << absl::StreamFormat("%*s ", size_len_, BigNumber(entry.glob_entry.FileSize()));
    }
    if (show_type_) {
      std::cout << GetType(entry.glob_entry.entry) << " ";
    }
    if (show_depth_) {
      std::cout << absl::StreamFormat("%*s ", depth_len_, BigNumber(entry.glob_entry.depth));
    }
    std::cout << entry.glob_entry.entry.path().lexically_relative(root_).native() << "\n";
  }

  void ComputeFieldLengths() const {
    size_len_ = std::max(size_len_, BigNumberLen(size_max_));
    depth_len_ = std::max(depth_len_, BigNumberLen(depth_max_));
  }

  void PrintFastEntry(const Entry& entry) const {
    ComputeFieldLengths();
    PrintEntry(entry);
  }

  void PrintAllEntries() const {
    ComputeFieldLengths();
    for (const auto& entry : entries_) {
      PrintEntry(entry);
    }
  }

  void PrintSummary(bool show_extensions = false) const {
    std::size_t name_len{1};
    unsigned val_len{1};
    absl::btree_map<std::string, std::size_t> data;
    if (show_extensions) {
      for (const auto& [name, count] : extensions_) {
        const auto len = data.emplace(absl::StrCat(name, ":"), count).first->first.size();
        name_len = std::max(name_len, len);
        val_len = std::max(val_len, BigNumberLen(count));
      }
    }
    for (const auto& [name, value] : stats_) {
      const auto len = data.emplace(absl::StrCat(name, ":"), value).first->first.size();
      name_len = std::max(name_len, len);
      val_len = std::max(val_len, BigNumberLen(value));
    }
    for (const auto& [name, value] : data) {
      std::cout << absl::StrFormat("%-*s %*s\n", name_len, name, val_len, BigNumber(value));
    }
  }

 private:
  const std::filesystem::path root_;
  const bool dotdir_{true};
  const bool dotfile_{true};
  const bool show_fast_{false};
  const bool show_size_{false};
  const bool show_type_{false};
  const bool show_depth_{false};
  const bool sum_extensions_{false};
  const std::size_t sum_every_{0};
  absl::btree_set<Entry> entries_;
  absl::btree_map<std::string, std::size_t> extensions_;
  std::size_t dirs_{0};
  std::size_t links_{0};
  std::size_t files_{0};
  std::size_t other_{0};
  std::size_t size_{0};
  std::size_t size_max_{0};
  mutable unsigned size_len_{1};
  std::size_t depth_max_{0};
  mutable unsigned depth_len_{1};
  std::size_t seen_{0};
  const absl::btree_map<std::string, std::size_t&> stats_;
};

}  // namespace

constexpr std::string_view kUsage = R"usage(glob [<flags>*] [<root_path>] <pattern>

Glog is a simple recursive file finder that can produce a summary. If a pattern
is given then it follows `fnmame` convention or Google's RE2 if --re2 is set.
If no root_path is given, then the <pattern> argument will be split to produce a
root by finding the last directory component that is not itself a pattern.

The default glob expressions support:
- '*':          Any number of characters not including '/'.
- '**':         Any number of characters including '/'.
- '?':          A single character (not '/').
- '\?':         the character '?'. Note that all characters can be escaped.
- '[<range>]':  Requiring the given <range> or ranges.
- '[!<range>]': Excluding the given <range> or ranges.
- '[]':         An empty range is not allowed.
                This means that ']' can be allowed as the first character of a
                range, e.g. '[!]]' allows all but ']'.

A range is interpreted as follows:
- 'x':          The single character x.
- 'x-z':        Any character from 'x' to 'z' inclusive.
- 'x-' or '-x': The characters 'x' and '-'.

Examples:

- Show all files under the current directory with their sizes but un-aligned:
    glob . --fast --size

- Show only a summary of all files:
    glob . --noentries --sum

- Show only a summary for all files excluding all directories and files starting
  with a '.' while showing stats per file extension:
    glob . --nodotdir --nodotfile --noentries --sum_extensions

- Show only a summary of files under the current directory whose extension is
  one of [.cc, .cpp, .h] and which are not under any directory that starts with
  a dot (.):
    glob . '.*[.](cc|cpp|h)' --nodotdir --noentries --re2 --sum
    glob . '([^/]|/[^.])*[.](cc|cpp|h)' --noentries --re2 --sum

Flags:)usage";

// NOLINTNEXTLINE(*-macro-usage)
#define EXIT_IF_ERROR(error_status)                                           \
  if (!(error_status).ok()) {                                                 \
    std::cerr << "ERROR: " << ::mbo::status::GetStatus(error_status) << "\n"; \
    return 1;                                                                 \
  }

int main(int argc, char** argv) {
  absl::SetProgramUsageMessage(kUsage);
  const std::vector<char*> args = absl::ParseCommandLine(argc, argv);
  if (args.size() < 2 || args.size() > 3) {
    std::cerr << "Requires at most 2 arguments: glob [<path>] <pattern>\n";
    return 1;
  }
  if (absl::GetFlag(FLAGS_sum_every) > 0 || absl::GetFlag(FLAGS_sum_extensions)) {
    absl::SetFlag(&FLAGS_sum, true);
  }
  const auto root = std::filesystem::path(args.at(1)).lexically_normal();
  const absl::StatusOr<mbo::file::RootAndPattern> root_pattern =
      args.size() == 2  // NL
          ? mbo::file::GlobSplit(root)
          : mbo::file::RootAndPattern{.root = std::string{root}, .pattern = args.at(2)};
  EXIT_IF_ERROR(root_pattern);
  Entries entries(root_pattern->root);

  const auto collect = std::bind_front(&Entries::Add, &entries);
  const auto result = absl::GetFlag(FLAGS_re2)  // NL
                          ? mbo::file::GlobRe2(root_pattern, {}, collect)
                          : mbo::file::Glob(root_pattern, {}, {}, collect);
  EXIT_IF_ERROR(result);
  if (absl::GetFlag(FLAGS_entries) && !absl::GetFlag(FLAGS_fast)) {
    entries.PrintAllEntries();
  }
  if (absl::GetFlag(FLAGS_sum)) {
    std::cout << "\n";
    entries.PrintSummary(absl::GetFlag(FLAGS_sum_extensions));
  }
  return 0;
}
