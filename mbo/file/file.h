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

#ifndef MBO_FILE_FILE_H_
#define MBO_FILE_FILE_H_

#include <filesystem>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace mbo::file {

// Normalize `path` (wraps `std::filesystem::lexically_normal`).
// - '//' -> '/' (empty parameters will be removed).
// - no trailing '/' but preserves a single (windows 'C:/' -> 'C:').
// - starting '/' to handle absolute files (for linux, etc).
std::filesystem::path NormalizePath(const std::filesystem::path& path);

// Return true if path is an absolute path.
inline bool IsAbsolutePath(const std::filesystem::path& path) {
  return path.is_absolute();
}

// Combine all parameters as if they were path elements.
// Normalizes the result using `NormalizePath`.
// Any path component that is absolute will simply be treated as relative and concatenated.
template<typename... T>
inline std::filesystem::path JoinPaths(const std::filesystem::path& first_path, const T&... paths) {
  return NormalizePath([&] {
    if constexpr (sizeof...(paths) == 0) {
      return std::filesystem::path(first_path);
    } else if (first_path.empty()) {
      return JoinPaths(paths...);
    } else {
      const auto joined = JoinPaths(paths...);
      if (joined.is_absolute()) {
        return first_path / joined.relative_path();
      } else {
        return first_path / joined;
      }
    }
  }());
}

// Combine all parameters as if they were path elements but respects absolute path elements.
// Normalizes the result using `NormalizePath`.
// Any path component that is absolute will drop anything to its left..
template<typename... T>
inline std::filesystem::path JoinPathsRespectAbsolute(const std::filesystem::path& first_path, const T&... paths) {
  return NormalizePath([&] {
    if constexpr (sizeof...(paths) == 0) {
      return std::filesystem::path(first_path);
    } else if (first_path.empty()) {
      return JoinPaths(paths...);
    } else {
      // In theory this is what `fs::path /=` should do. But then it does way more.
      auto joined = JoinPathsRespectAbsolute(paths...);
      if (joined.is_absolute()) {
        return joined;
      } else {
        return first_path / joined;
      }
    }
  }());
}

// Writes `content` to the file `file_name`, overwriting any existing content.
// Fails if directory does not exist.
//
// NOTE: Will return OK if all of the data in `content` was written.
// May write some of the data and return an error.
//
// Typical return codes:
//  * absl::StatusCode::kOk:      Write operation was successful.
//  * absl::StatusCode::kUnknown: A Write, a Close or Open error occurred.
absl::Status SetContents(const std::filesystem::path& file_name, std::string_view content);

// Answers the question, "Does the named file exist, and is it readable?"
//
// Typical return codes:
//  * absl::StatusCode::kOk:                The file exists and is readable.
//  * absl::StatusCode::kNotFound:          The file does not exist.
//  * absl::StatusCode::FailedPrecondition: The file is not readable.
//  * absl::StatusCode::kUnknown:           Other error
//
// NOTE: The behavior is undefined if called on a directory.
absl::Status Readable(const std::filesystem::path& file_name);

// Read the contents of the file `file_name` and return them.
//
// Returns:
//  * string:                          The contents
//  * absl::StatusCode::kUnknownError: File does not exist or other error.
absl::StatusOr<std::string> GetContents(const std::filesystem::path& file_name);

// Read the contents of file `file_name` on a line by line basis and stop
// when reaching `max_lines` or the file ends. The returned string will contain
// a new-line at the end as long as either reading stopped prior to the end of
// the file or if the file ended in a new-line. That is the function will not
// append a new-line where there was no new-line in the file. That in turn means
// that a non text-file does not become a text-file in memory.
//
// Returns:
//  * string:                          The contents
//  * absl::StatusCode::kUnknownError: File does not exist or other error.
absl::StatusOr<std::string> GetMaxLines(const std::filesystem::path& file_name, std::size_t max_lines);

// Return the last modified time.
//
// Returns:
//  * absl::Time:          The last modified time
//  * absl::NotFoundError: File access error.
absl::StatusOr<absl::Time> GetMTime(const std::filesystem::path& file_name);

}  // namespace mbo::file

#endif  // MBO_FILE_FILE_H_
