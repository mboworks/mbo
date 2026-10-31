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

#include "mbo/testing/runfiles_dir.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "mbo/file/file.h"
#include "mbo/status/status_macros.h"
#include "rules_cc/cc/runfiles/runfiles.h"

namespace mbo::testing {
namespace {
std::string SafeStr(const char* str, std::string_view default_str) {
  if (str != nullptr) {
    return {str};
  }
  return std::string(default_str);
}
}  // namespace

using rules_cc::cc::runfiles::Runfiles;

absl::StatusOr<std::string> RunfilesDir(std::string_view source) {
  if (source.starts_with('@')) {
    std::pair<std::string_view, std::string> parts = absl::StrSplit(source, absl::MaxSplits("//", 1));
    parts.first.remove_prefix(1);
    if (const auto pos = parts.second.find(':'); pos != std::string::npos) {
      parts.second.at(pos) = '/';
    }
    return RunfilesDir(parts.first, parts.second);
  }
  if (source.starts_with("//")) {
    source.remove_prefix(2);
    std::string source_rel(source);
    if (const auto pos = source_rel.find(':'); pos != std::string::npos) {
      source_rel.at(pos) = '/';
    }
    return RunfilesDir("", source_rel);
  }
  return RunfilesDir("", source);
}

absl::StatusOr<std::string> RunfilesDir(std::string_view workspace, std::string_view source_rel) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const std::string workspace_env = SafeStr(getenv("TEST_WORKSPACE"), workspace);

  std::string error;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(std::string(workspace_env), &error));
  if (runfiles == nullptr) {
    return absl::NotFoundError(absl::StrCat("Could not determine runfiles directory: ", error));
  }
  if (!workspace.empty() && workspace != workspace_env) {
    // Must lookup the workspace and translate it.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const std::string test_bin = SafeStr(getenv("TEST_SRCDIR"), workspace);
    if (test_bin.empty()) {
      return absl::NotFoundError("Environment variable `TEST_SRCDIR` not present.");
    }
    static constexpr std::string_view kRunfiles = ".runfiles";
    if (!test_bin.ends_with(kRunfiles)) {
      return absl::NotFoundError(
          absl::StrFormat("Environment variable `TEST_SRCDIR` does not end in '.runfiles', got '%s'.", test_bin));
    }
    const std::string mapping_file = absl::StrCat(test_bin, "/_repo_mapping");
    MBO_ASSIGN_OR_RETURN(const std::string mapping, mbo::file::GetContents(mapping_file));
    for (const std::string_view line : absl::StrSplit(mapping, '\n')) {
      const std::vector<std::string_view> parts = absl::StrSplit(line, ',', absl::AllowEmpty());
      if (parts.size() == 3 && parts.at(1) == workspace) {
        return runfiles->Rlocation(mbo::file::JoinPaths(parts.at(2), source_rel));
      }
    }
    auto result = runfiles->Rlocation(mbo::file::JoinPaths(workspace, source_rel));
    if (result.empty()) {
      result = runfiles->Rlocation(mbo::file::JoinPaths(workspace_env, source_rel));
    }
    if (!result.empty()) {
      return result;
    }
    return absl::NotFoundError(absl::StrFormat("Repo '%s' not found in mapping.", workspace));
  }
  return runfiles->Rlocation(mbo::file::JoinPaths(workspace_env, source_rel));
}

std::string RunfilesDirOrDie(std::string_view source) {
  const auto runfiles_dir = RunfilesDir(source);
  ABSL_QCHECK_OK(runfiles_dir);
  return *runfiles_dir;
}

std::string RunfilesDirOrDie(std::string_view workspace, std::string_view source_rel) {
  const auto runfiles_dir = RunfilesDir(workspace, source_rel);
  ABSL_QCHECK_OK(runfiles_dir);
  return *runfiles_dir;
}

}  // namespace mbo::testing
