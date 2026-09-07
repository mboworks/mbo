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

#include "mbo/log/log_timing.h"

#include <cstddef>
#include <string>
#include <string_view>

#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/str_replace.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

// NOLINTBEGIN(*avoid-non-const-global-variables,*abseil-no-namespace)

ABSL_FLAG(
    absl::Duration,
    mbo_log_timing_min_duration,
    absl::Seconds(2),
    "The minimum duration for a `LogTiming` statement to actually be logged.");

ABSL_FLAG(
    absl::LogSeverity,
    mbo_log_timing_min_severity_always,
    absl::LogSeverity::kError,
    "The minimum severity at which the duration will be ignored.");

// NOLINTEND(*avoid-non-const-global-variables,*abseil-no-namespace)

namespace mbo::log::log_internal {

namespace {

std::string ShortenLambdas(std::string_view function) {
  std::string result = absl::StrReplaceAll(
      function, {
                    {"::(anonymous class)::operator()()", "::[]()"},
                    {"::(anonymous class)::operator()", "::[]()"},
                });
  return result;
}

}  // namespace

std::string_view LogTimingImpl::ReverseFindSpaceSkipPastMatchingBrackets(std::string_view str) {
  // Required before `length() - 1`: removing this guard underflows for empty input.
  if (str.empty()) {
    return str;
  }
  std::string_view::size_type pos = str.length() - 1;
  std::size_t brackets = 0;
  std::size_t angles = 0;
  for (; pos != 0; --pos) {
    switch (str.at(pos)) {
      default: break;
      case ')': ++brackets; break;
      case '(': --brackets; break;
      case '>': ++angles; break;
      case '<': --angles; break;
      case ' ': {
        if (brackets == 0 && angles == 0) {
          str.remove_prefix(pos + 1);
          return str;
        }
        break;
      }
    }
  }
  return str;
}

std::string_view LogTimingImpl::ReverseStripAngleBrackets(std::string_view str) {
  // Required before `length() - 1`: removing this guard underflows for empty input.
  if (str.empty()) {
    return str;
  }
  std::string_view::size_type pos = str.length() - 1;
  std::size_t angles = 0;
  for (; pos != 0; --pos) {
    switch (str.at(pos)) {
      default: break;
      case '>': ++angles; break;
      case '<': {
        --angles;
        if (angles == 0) {
          str.remove_suffix(str.length() - pos);
          return str;
        }
        break;
      }
    }
  }
  return str;
}

std::string LogTimingImpl::StripFunctionName(std::string_view function) {
  auto pos = function.rfind(')');
  if (pos == std::string_view::npos) {
    return std::string(function);
  }
  for (std::size_t level = 0; pos != 0; --pos) {
    if (function.at(pos) == ')') {
      ++level;
    } else if (function.at(pos) == '(') {
      --level;
      if (level == 0) {
        static constexpr std::string_view kConversion = "operator()";
        if (function.substr(0, pos + 2).ends_with(kConversion)) {
          function.remove_suffix(function.length() - (pos + 2));
          pos -= kConversion.length();
          continue;
        }
        if (pos > 0 && function.at(pos - 1) == ':') {
          continue;
        }
        break;
      }
    }
  }
  if (pos > 0) {
    function.remove_suffix(function.length() - pos);
  }
  if (function.ends_with('>')) {
    function = ReverseStripAngleBrackets(function);
  }
  function = ReverseFindSpaceSkipPastMatchingBrackets(function);
  return ShortenLambdas(function);
}

void LogTimingImpl::Log() const {
  const absl::Duration min_duration = args_.min_duration.value_or(absl::GetFlag(FLAGS_mbo_log_timing_min_duration));
  const absl::Duration duration = absl::Now() - args_.start_time;
  if (duration < min_duration && args_.severity < absl::GetFlag(FLAGS_mbo_log_timing_min_severity_always)) {
    return;
  }

  LOG(LEVEL(args_.severity)).AtLocation(args_.src.file_name(), static_cast<int>(args_.src.line()))
      << "LogTiming(" << duration << " @ " << StripFunctionName(args_.src.function_name()) << ")"
      << (message_.empty() ? "" : ": ") << message_;
}

}  // namespace mbo::log::log_internal
