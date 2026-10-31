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

#include "mbo/file/glob.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "mbo/status/status_macros.h"
#include "mbo/types/extend.h"
#include "re2/re2.h"

#ifdef MBO_ALWAYS_INLINE
# undef MBO_ALWAYS_INLINE
#endif
#ifdef NDEBUG
# define MBO_ALWAYS_INLINE __attribute__((always_inline)) inline
#else
# define MBO_ALWAYS_INLINE
#endif

namespace fs = std::filesystem;

namespace mbo::file {
namespace {

using CharacterSet = std::array<bool, 256>;

void AddCharacterClass(std::string_view name, CharacterSet& chars) {
  for (std::size_t value = 0; value < 128; ++value) {
    const auto chr = static_cast<unsigned char>(value);
    const bool alpha = ('a' <= chr && chr <= 'z') || ('A' <= chr && chr <= 'Z');
    const bool digit = '0' <= chr && chr <= '9';
    const bool graph = 0x21 <= chr && chr <= 0x7e;
    const bool match =
        name == "ascii" || (name == "alnum" && (alpha || digit)) || (name == "alpha" && alpha)
        || (name == "blank" && (chr == ' ' || chr == '\t')) || (name == "cntrl" && (chr < 0x20 || chr == 0x7f))
        || (name == "digit" && digit) || (name == "graph" && graph) || (name == "lower" && 'a' <= chr && chr <= 'z')
        || (name == "print" && 0x20 <= chr && chr <= 0x7e) || (name == "punct" && graph && !alpha && !digit)
        || (name == "space" && (chr == ' ' || ('\t' <= chr && chr <= '\r')))
        || (name == "upper" && 'A' <= chr && chr <= 'Z') || (name == "word" && (alpha || digit || chr == '_'))
        || (name == "xdigit" && (digit || ('a' <= chr && chr <= 'f') || ('A' <= chr && chr <= 'F')));
    chars.at(chr) = chars.at(chr) || match;
  }
}

absl::Status ValidateCharacterClass(std::string_view name) {
  static constexpr std::array<std::string_view, 14> kNames{
      "alnum", "alpha", "ascii", "blank", "cntrl", "digit", "graph",
      "lower", "print", "punct", "space", "upper", "word",  "xdigit",
  };
  if (name.empty()) {
    return absl::InvalidArgumentError("Invalid empty character-class name.");
  }
  if (!absl::c_linear_search(kNames, name)) {
    return absl::InvalidArgumentError(absl::StrFormat("Unsupported character-class name '%s'.", name));
  }
  return absl::OkStatus();
}

absl::StatusOr<unsigned char> TakeRangeCharacter(std::string_view& pattern) {
  // Callers only enter with a member available; a trailing escape is diagnosed below.
  if (pattern.front() != '\\') {
    const auto result = static_cast<unsigned char>(pattern.front());
    pattern.remove_prefix(1);
    return result;
  }
  if (pattern.size() == 1) {
    return absl::InvalidArgumentError("No character left to escape at end of pattern.");
  }
  const auto result = static_cast<unsigned char>(pattern.at(1));
  pattern.remove_prefix(2);
  return result;
}

void AppendRangeCharacter(std::string& output, unsigned char chr) {
  if (chr < 0x20 || chr >= 0x7f) {
    absl::StrAppendFormat(&output, "\\x%02x", chr);
  } else {
    if (chr == '\\' || chr == ']' || chr == '-' || chr == '^') {
      output += '\\';
    }
    output += static_cast<char>(chr);
  }
}

void AppendCharacterSet(const CharacterSet& chars, bool negative, std::string& output) {
  output += negative ? "[^" : "[";
  for (std::size_t first = 0; first < chars.size();) {
    if (!chars.at(first)) {
      ++first;
      continue;
    }
    std::size_t last = first;
    while (last + 1 < chars.size() && chars.at(last + 1)) {
      ++last;
    }
    AppendRangeCharacter(output, static_cast<unsigned char>(first));
    if (last >= first + 2) {
      output += '-';
      AppendRangeCharacter(output, static_cast<unsigned char>(last));
    } else if (last == first + 1) {
      AppendRangeCharacter(output, static_cast<unsigned char>(last));
    }
    first = last + 1;
  }
  output += ']';
}

// The parser necessarily branches for POSIX members, ranges, negation, and diagnostics.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
absl::Status GlobFindRange(std::string_view& pattern, std::string& re2_pattern) {
  pattern.remove_prefix(1);
  bool negative = false;
  if (pattern.starts_with('!')) {
    negative = true;
    pattern.remove_prefix(1);
  }
  CharacterSet chars{};
  if (pattern.starts_with(']')) {
    chars.at(static_cast<unsigned char>(']')) = true;
    pattern.remove_prefix(1);
  }
  while (!pattern.empty() && !pattern.starts_with(']')) {
    if (pattern.starts_with("[.") || pattern.starts_with("[=")) {
      return absl::InvalidArgumentError("Locale collating symbols and equivalence classes are unsupported.");
    }
    if (pattern.starts_with("[:")) {
      const std::size_t end = pattern.find(":]", 2);
      if (end == std::string_view::npos) {
        return absl::InvalidArgumentError("Unterminated character-class.");
      }
      const std::string_view name = pattern.substr(2, end - 2);
      MBO_RETURN_IF_ERROR(ValidateCharacterClass(name));
      AddCharacterClass(name, chars);
      pattern.remove_prefix(end + 2);
      continue;
    }
    MBO_ASSIGN_OR_RETURN(const unsigned char first, TakeRangeCharacter(pattern));
    if (pattern.starts_with('-') && pattern.size() > 1 && pattern.at(1) != ']') {
      pattern.remove_prefix(1);
      MBO_ASSIGN_OR_RETURN(const unsigned char last, TakeRangeCharacter(pattern));
      if (last < first) {
        return absl::InvalidArgumentError(absl::StrFormat("Descending range '%c-%c' is unsupported.", first, last));
      }
      for (std::size_t chr = first; chr <= last; ++chr) {
        chars.at(chr) = true;
      }
    } else {
      chars.at(first) = true;
    }
  }
  if (pattern.empty()) {
    return absl::InvalidArgumentError("Unterminated range expression.");
  }
  pattern.remove_prefix(1);
  chars.at(static_cast<unsigned char>('/')) = false;
  if (negative) {
    chars.at(static_cast<unsigned char>('/')) = true;
  } else if (!absl::c_any_of(chars, [](bool value) { return value; })) {
    return absl::InvalidArgumentError("Range expression matches only the path separator.");
  }
  AppendCharacterSet(chars, negative, re2_pattern);
  return absl::OkStatus();
}

struct GlobData : mbo::types::Extend<GlobData> {
  std::string pattern;
  std::optional<std::size_t> path_len;
  std::size_t root_len = 0;
  bool mixed = false;
};

bool AllowApendStar(std::string_view pattern, std::size_t past_last_escape, const Glob2Re2Options& options) {
  // We consider whether the previous chars were stars, but they could have been escaped.
  // The escape char itself could be escaped, so we forward search for escape chars and drop those
  // nad the next character. What remains is an unescaped sequence.
  pattern.remove_prefix(past_last_escape);
  if (options.allow_star_star) {
    return !pattern.ends_with("**");
  } else {
    return !pattern.ends_with('*');
  }
}

bool AllowAppendSlash(std::string_view pattern, std::size_t past_last_escape) {
  pattern.remove_prefix(past_last_escape);
  return !pattern.ends_with('/') || pattern.ends_with(":/");
}

// Deduplicate '/'s, remove trailing '/' and reduce '*' sequences to at most `options.allow_star_star ? 2 : 1`.
MBO_ALWAYS_INLINE absl::StatusOr<std::string> GlobNormalizeStr(
    std::string_view glob_pattern,
    const Glob2Re2Options& options) {
  std::string result;
  result.reserve(glob_pattern.size());
  std::size_t past_last_escape = 0;
  while (!glob_pattern.empty()) {
    char chr = glob_pattern.front();
    glob_pattern.remove_prefix(1);
    if (chr == '\\') {
      if (glob_pattern.empty()) {
        return absl::InvalidArgumentError("No character left to escape at end of pattern.");
      }
      chr = glob_pattern.front();
      glob_pattern.remove_prefix(1);
      if (chr != '/') {
        result += '\\';
        result += chr;
        past_last_escape = result.size();
        continue;
      }
      // No need to escape the forward slash, but rather prevent duplicates even if escaped.
    }
    switch (chr) {
      case '*':
        if (AllowApendStar(result, past_last_escape, options)) {
          result += chr;
        }
        continue;
      case '[':
        if (!options.allow_ranges || !glob_pattern.starts_with("/]")) {
          result += chr;  // replace '[/]' with '/'.
          continue;
        }
        glob_pattern.remove_prefix(2);
        chr = '/';
        [[fallthrough]];
      case '/':
        if (AllowAppendSlash(result, past_last_escape)) {
          result += chr;
        }
        continue;
      default: {
        result += chr;
        continue;
      }
    }
  }
  if (result.size() > 1 && result.ends_with('/')) {
    result.pop_back();
  }
  return result;
}

MBO_ALWAYS_INLINE absl::StatusOr<GlobData> GlobNormalizeData(
    std::string_view glob_pattern,
    const Glob2Re2Options& options) {
  MBO_MOVE_TO_OR_RETURN(GlobNormalizeStr(glob_pattern, options), const std::string pattern);
  char chr = '\0';
  std::size_t slash_0 = std::string_view::npos;
  std::size_t slash_1 = std::string_view::npos;
  bool found_pattern = false;
  glob_pattern = pattern;  // Operate on the normalized pattern
  GlobData result{
      .pattern = pattern,
  };
  while (!glob_pattern.empty()) {
    chr = glob_pattern.front();
    switch (chr) {
      case '\\': {
        glob_pattern.remove_prefix(2);
        continue;
      }
      case '*':
      case '?':
        glob_pattern.remove_prefix(1);
        found_pattern = true;
        continue;
      case '[': {
        found_pattern = true;
        if (!options.allow_ranges) {
          glob_pattern.remove_prefix(1);
          continue;
        }
        std::string tmp_re2;
        MBO_RETURN_IF_ERROR(GlobFindRange(glob_pattern, tmp_re2));
        continue;
      }
      case '/':
        slash_0 = slash_1;
        slash_1 = result.pattern.size() - glob_pattern.size();
        if (!found_pattern) {
          result.root_len = result.pattern.size() - glob_pattern.size();
        }
        glob_pattern.remove_prefix(1);
        continue;
      default: glob_pattern.remove_prefix(1); continue;
    }
  }
  if (!found_pattern) {
    result.root_len = result.pattern.size();
  }
  if (result.pattern.size() > 1 && result.pattern.ends_with('/')) {
    result.pattern.pop_back();
    slash_1 = slash_0;
  }
  if (slash_1 == std::string_view::npos) {
    return result;
  }
  const bool star_star = result.pattern.find("**", slash_1) != std::string::npos;
  result.path_len = star_star ? result.pattern.size() : slash_1;
  return result;
}

MBO_ALWAYS_INLINE void Glob2Re2ExpressionImplStar(std::string& re2_pattern, std::string_view& pattern) {
  std::size_t stars = 0;
  while (stars < pattern.size() && pattern.at(stars) == '*') {
    ++stars;
  }
  const bool complete_component = stars >= 2 && (re2_pattern.empty() || re2_pattern.ends_with('/'))
                                  && (stars == pattern.size() || pattern.at(stars) == '/');
  if (!complete_component) {
    pattern.remove_prefix(stars);
    absl::StrAppend(&re2_pattern, "[^/]*");
    return;
  }
  pattern.remove_prefix(stars);
  if (re2_pattern.ends_with('/')) {
    if (pattern.empty()) {
      absl::StrAppend(&re2_pattern, ".*");
      return;
    }
    re2_pattern.pop_back();
    absl::StrAppend(&re2_pattern, "(?:/[^/]+)*");
    if (pattern.starts_with('/')) {
      re2_pattern += '/';
      pattern.remove_prefix(1);
    }
    return;
  }
  if (pattern.starts_with('/')) {
    absl::StrAppend(&re2_pattern, "(?:[^/]+/)*");
    pattern.remove_prefix(1);
  } else {
    absl::StrAppend(&re2_pattern, ".*");
  }
}

void AppendLiteral(std::string& re2_pattern, char chr) {
  constexpr std::string_view kRe2Metacharacters = R"(.+*?()|[]{}^$\)";
  if (absl::StrContains(kRe2Metacharacters, chr)) {
    re2_pattern += '\\';
  }
  re2_pattern += chr;
}

void SkipRange(std::string_view pattern, std::size_t& pos) {
  ++pos;
  if (pos < pattern.size() && pattern.at(pos) == '!') {
    ++pos;
  }
  if (pos < pattern.size() && pattern.at(pos) == ']') {
    ++pos;
  }
  while (pos < pattern.size() && pattern.at(pos) != ']') {
    if (pattern.at(pos) == '\\' && pos + 1 < pattern.size()) {
      pos += 2;
    } else if (pattern.at(pos) == '[' && pos + 1 < pattern.size() && pattern.at(pos + 1) == ':') {
      const std::size_t end = pattern.find(":]", pos + 2);
      pos = end == std::string_view::npos ? pattern.size() : end + 2;
    } else {
      ++pos;
    }
  }
  if (pos < pattern.size()) {
    ++pos;
  }
}

absl::StatusOr<std::string> Glob2Re2ExpressionImpl(std::string_view pattern, const Glob2Re2Options& options);

constexpr std::size_t kMaxBraceSequenceTerms = 10'000;

bool IsIntegerSequenceEndpoint(std::string_view endpoint) {
  if (endpoint.starts_with('+') || endpoint.starts_with('-')) {
    endpoint.remove_prefix(1);
  }
  return !endpoint.empty() && absl::c_all_of(endpoint, absl::ascii_isdigit);
}

std::size_t IntegerDigitWidth(std::string_view endpoint) {
  if (endpoint.starts_with('+') || endpoint.starts_with('-')) {
    endpoint.remove_prefix(1);
  }
  return endpoint.size();
}

bool HasLeadingZero(std::string_view endpoint) {
  if (endpoint.starts_with('+') || endpoint.starts_with('-')) {
    endpoint.remove_prefix(1);
  }
  return endpoint.size() > 1 && endpoint.starts_with('0');
}

std::string FormatSequenceInteger(std::int64_t value, std::size_t width, bool zero_pad) {
  std::string result = std::to_string(value);
  if (!zero_pad) {
    return result;
  }
  const bool negative = result.starts_with('-');
  const std::size_t digits = result.size() - static_cast<std::size_t>(negative);
  if (digits >= width) {
    return result;
  }
  result.insert(static_cast<std::size_t>(negative), width - digits, '0');
  return result;
}

void AppendSequenceTerm(std::string& re2_pattern, std::string_view term, bool first) {
  if (!first) {
    re2_pattern += '|';
  }
  for (const char chr : term) {
    AppendLiteral(re2_pattern, chr);
  }
}

absl::StatusOr<std::string> BuildIntegerSequence(
    std::string_view first_text,
    std::string_view last_text,
    std::int64_t first,
    std::int64_t last) {
  const bool zero_pad = HasLeadingZero(first_text) || HasLeadingZero(last_text);
  const std::size_t width = std::max(IntegerDigitWidth(first_text), IntegerDigitWidth(last_text));
  std::string result = "(?:";
  std::size_t terms = 0;
  for (std::int64_t value = first;; value += first <= last ? 1 : -1) {
    if (terms == kMaxBraceSequenceTerms) {
      return absl::InvalidArgumentError(
          absl::StrCat("Brace sequence exceeds the ", kMaxBraceSequenceTerms, "-term limit."));
    }
    AppendSequenceTerm(result, FormatSequenceInteger(value, width, zero_pad), terms == 0);
    ++terms;
    if (value == last) {
      break;
    }
  }
  result += ')';
  return result;
}

std::string BuildCharacterSequence(char first_char, char last_char) {
  const int first = static_cast<unsigned char>(first_char);
  const int last = static_cast<unsigned char>(last_char);
  std::string result = "(?:";
  std::size_t terms = 0;
  for (int value = first;; value += first <= last ? 1 : -1) {
    const char chr = static_cast<char>(value);
    AppendSequenceTerm(result, std::string_view(&chr, 1), terms == 0);
    ++terms;
    if (value == last) {
      break;
    }
  }
  result += ')';
  return result;
}

// A comma-less SHGLOB brace is a sequence only for two integer endpoints or two ASCII letter
// endpoints separated by `..`. Other comma-less braces remain literal, matching shell behavior.
absl::StatusOr<bool> AppendBraceSequence(std::string& re2_pattern, std::string_view body) {
  const std::size_t separator = body.find("..");
  if (separator == std::string_view::npos || body.find("..", separator + 2) != std::string_view::npos) {
    return false;
  }
  const std::string_view first_text = body.substr(0, separator);
  const std::string_view last_text = body.substr(separator + 2);
  const bool integer_sequence = IsIntegerSequenceEndpoint(first_text) && IsIntegerSequenceEndpoint(last_text);
  const bool character_sequence = first_text.size() == 1 && last_text.size() == 1
                                  && absl::ascii_isalpha(first_text.front()) && absl::ascii_isalpha(last_text.front());
  if (!integer_sequence && !character_sequence) {
    return false;
  }

  std::int64_t first_integer = 0;
  std::int64_t last_integer = 0;
  if (integer_sequence
      && (!absl::SimpleAtoi(first_text, &first_integer) || !absl::SimpleAtoi(last_text, &last_integer))) {
    return false;
  }

  if (integer_sequence) {
    MBO_ASSIGN_OR_RETURN(
        const std::string sequence, BuildIntegerSequence(first_text, last_text, first_integer, last_integer));
    re2_pattern += sequence;
  } else {
    re2_pattern += BuildCharacterSequence(first_text.front(), last_text.front());
  }
  return true;
}

absl::StatusOr<bool> AppendClosedBraceGroup(
    std::string& re2_pattern,
    std::string_view pattern,
    std::size_t start,
    std::size_t scan,
    bool saw_comma,
    std::vector<std::pair<std::size_t, std::size_t>>& alternatives,
    std::size_t& pos,
    const Glob2Re2Options& options) {
  if (!saw_comma) {
    MBO_ASSIGN_OR_RETURN(const bool consumed, AppendBraceSequence(re2_pattern, pattern.substr(start, scan - start)));
    if (consumed) {
      pos = scan + 1;
    }
    return consumed;
  }
  alternatives.emplace_back(start, scan);
  re2_pattern += "(?:";
  for (std::size_t idx = 0; idx < alternatives.size(); ++idx) {
    if (idx != 0) {
      re2_pattern += '|';
    }
    MBO_ASSIGN_OR_RETURN(
        const std::string alternative,
        Glob2Re2ExpressionImpl(
            pattern.substr(alternatives.at(idx).first, alternatives.at(idx).second - alternatives.at(idx).first),
            options));
    re2_pattern += alternative;
  }
  re2_pattern += ')';
  pos = scan + 1;
  return true;
}

absl::StatusOr<bool> AppendBraceGroup(
    std::string& re2_pattern,
    std::string_view pattern,
    std::size_t& pos,
    const Glob2Re2Options& options) {
  std::vector<std::pair<std::size_t, std::size_t>> alternatives;
  std::size_t start = pos + 1;
  std::size_t scan = start;
  std::size_t depth = 0;
  bool saw_comma = false;
  while (scan < pattern.size()) {
    if (pattern.at(scan) == '\\' && scan + 1 < pattern.size()) {
      scan += 2;
    } else if (pattern.at(scan) == '[') {
      SkipRange(pattern, scan);
    } else if (pattern.at(scan) == '{') {
      ++depth;
      ++scan;
    } else if (pattern.at(scan) == '}' && depth != 0) {
      --depth;
      ++scan;
    } else if (pattern.at(scan) == '}') {
      return AppendClosedBraceGroup(re2_pattern, pattern, start, scan, saw_comma, alternatives, pos, options);
    } else if (pattern.at(scan) == ',' && depth == 0) {
      alternatives.emplace_back(start, scan);
      start = scan + 1;
      saw_comma = true;
      ++scan;
    } else {
      ++scan;
    }
  }
  return false;
}

MBO_ALWAYS_INLINE absl::StatusOr<std::string> Glob2Re2ExpressionImpl(
    std::string_view pattern,
    const Glob2Re2Options& options) {
  std::string re2_pattern;
  re2_pattern.reserve(pattern.size() * 2);
  while (!pattern.empty()) {
    if (pattern.starts_with("!(")) {
      return absl::InvalidArgumentError(
          "Negative extglob is unsupported because RE2 cannot express subexpression complement.");
    }
    const char chr = pattern.front();
    switch (chr) {
      default:
        re2_pattern += chr;
        pattern.remove_prefix(1);
        continue;
      case '\\':
        // if (pattern.size() < 2) { already handled
        AppendLiteral(re2_pattern, pattern.at(1));
        pattern.remove_prefix(2);
        continue;
      case '*': {
        Glob2Re2ExpressionImplStar(re2_pattern, pattern);
        continue;
      }
      case '?':
        absl::StrAppend(&re2_pattern, "[^/]");
        pattern.remove_prefix(1);
        continue;
      case '{':
        if (options.syntax == GlobSyntax::kShGlob) {
          std::size_t pos = 0;
          MBO_ASSIGN_OR_RETURN(const bool consumed, AppendBraceGroup(re2_pattern, pattern, pos, options));
          if (consumed) {
            pattern.remove_prefix(pos);
            continue;
          }
        }
        [[fallthrough]];
      case '}':
      case '(':
      case ')':
      case '|':
      case '+':
      case '.':
      case '^':
      case '$':
        // Characters that need to be escaped since they are literal in rglob but special in re2.
        re2_pattern += '\\';
        re2_pattern += chr;
        pattern.remove_prefix(1);
        continue;
      case '[':
        if (!options.allow_ranges) {
          re2_pattern += pattern.front();
          pattern.remove_prefix(1);
          continue;
        }
        MBO_RETURN_IF_ERROR(GlobFindRange(pattern, re2_pattern));
        continue;
    }
  }
  return re2_pattern;
}

}  // namespace

namespace file_internal {

absl::StatusOr<GlobParts> GlobSplitParts(std::string_view pattern, const Glob2Re2Options& options) {
  MBO_MOVE_TO_OR_RETURN(GlobNormalizeData(pattern, options), const GlobData data);
  if (data.mixed) {
    return GlobParts{
        .path_pattern{data.pattern},
        .file_pattern{},
        .mixed = true,
    };
  }
  if (data.path_len.has_value()) {
    if (*data.path_len + 1 < data.pattern.size()) {
      return GlobParts{
          .path_pattern{data.pattern.substr(0, *data.path_len)},
          .file_pattern{data.pattern.substr(*data.path_len + 1)},
      };
    }
    return GlobParts{
        .path_pattern{data.pattern},
        .file_pattern{},
    };
  }
  return GlobParts{
      .path_pattern{},
      .file_pattern{data.pattern},
  };
}

}  // namespace file_internal

absl::StatusOr<std::string> Glob2Re2Expression(std::string_view pattern, const Glob2Re2Options& re2_convert_options) {
  MBO_MOVE_TO_OR_RETURN(GlobNormalizeStr(pattern, re2_convert_options), const std::string normalized_pattern);
  return Glob2Re2ExpressionImpl(normalized_pattern, re2_convert_options);
}

absl::StatusOr<std::unique_ptr<const RE2>> Glob2Re2(
    std::string_view pattern,
    const Glob2Re2Options& re2_convert_options) {
  MBO_MOVE_TO_OR_RETURN(Glob2Re2Expression(pattern, re2_convert_options), const std::string re2_pattern);
  auto re2 = std::make_unique<RE2>(re2_pattern, re2_convert_options.re2_options);
  if (re2->error_code() != RE2::NoError) {
    return absl::InvalidArgumentError(absl::StrFormat("Could not compile re2: '%s': %s.", pattern, re2->error()));
  }
  return re2;
}

namespace file_internal {

absl::StatusOr<RootAndPattern> GlobSplit(std::string_view pattern, const Glob2Re2Options& options) {
  MBO_MOVE_TO_OR_RETURN(GlobNormalizeData(pattern, options), const GlobData data);
  std::string_view root(data.pattern);
  root.remove_suffix(root.size() - data.root_len);
  std::string_view patt(data.pattern);
  patt.remove_prefix(data.root_len);
  if (patt.starts_with('/')) {
    patt.remove_prefix(1);
    if (root.empty()) {
      root = "/";
    }
  }
  return RootAndPattern{
      .root = std::string{root},
      .pattern = std::string{patt},
  };
}

}  // namespace file_internal

namespace {

template<typename FileIterator>
int FileIteratorDepth(const FileIterator& file_iterator) {
  if constexpr (std::same_as<FileIterator, std::filesystem::recursive_directory_iterator>) {
    return file_iterator.depth();
  } else {
    return 0;
  }
}

template<typename FileIterator>
void FileIteratorDisableRecursionPending(FileIterator& file_iterator) {
  if constexpr (std::same_as<FileIterator, std::filesystem::recursive_directory_iterator>) {
    file_iterator.disable_recursion_pending();
  }
}

template<typename FileIterator>
absl::Status GlobLoopImpl(const fs::path& root, const GlobOptions& options, const GlobEntryFunc& func) {
  const fs::path normalized_root = (root.empty() || root == ".") ? fs::current_path() : root.lexically_normal();
  std::error_code error_code;
  if (!fs::exists(normalized_root, error_code)) {
    return absl::NotFoundError(
        error_code ? error_code.message() : absl::StrFormat("Path does not exist: '%s'.", normalized_root));
  }
  auto it = FileIterator(normalized_root, options.dir_options, error_code);
  if (error_code) {
    return absl::CancelledError(error_code.message());
  }
  for (const fs::directory_entry& entry : it) {
    const GlobEntry glob_entry{
        .rel_path =
            options.use_rel_path ? std::optional<fs::path>{entry.path().lexically_relative(root)} : std::nullopt,
        .entry = entry,
        .depth = FileIteratorDepth(it),
    };
    MBO_ASSIGN_OR_RETURN(const GlobEntryAction action, func(glob_entry));
    switch (action) {
      case GlobEntryAction::kContinue: continue;
      case GlobEntryAction::kStop: return absl::OkStatus();
      case GlobEntryAction::kDoNotRecurse: {
        FileIteratorDisableRecursionPending(it);
        continue;
      }
    }
  }
  return absl::OkStatus();
}

absl::Status GlobLoop(const fs::path& root, const GlobOptions& options, const GlobEntryFunc& func) {
  if (options.recursive) {
    return GlobLoopImpl<fs::recursive_directory_iterator>(root, options, func);
  } else {
    return GlobLoopImpl<fs::directory_iterator>(root, options, func);
  }
}

}  // namespace

absl::Status GlobRe2(fs::path root, const RE2& regex, const GlobOptions& options, const GlobEntryFunc& func) {
  if (options.use_current_dir) {
    if (root.empty() || root == ".") {
      root = std::filesystem::current_path();
    } else if (root.is_relative()) {
      root = std::filesystem::current_path() / root;
    }
  }
  const auto wrap_func = [&](const GlobEntry& entry) -> absl::StatusOr<GlobEntryAction> {
    if (!regex.pattern().empty()) {
      if (options.use_rel_path) {
        if (!RE2::FullMatch(entry.MaybeRelativePath().native(), regex)) {
          return GlobEntryAction::kContinue;  // Path/Filename rejected.
        }
      } else {
        // The only difference is that we are not actually saving the `rel_path` anywhere.
        const fs::path rel_path = entry.entry.path().lexically_relative(root);
        if (!RE2::FullMatch(rel_path.native(), regex)) {
          return GlobEntryAction::kContinue;  // Path/Filename rejected.
        }
      }
    }
    return func(entry);
  };
  return GlobLoop(root, options, wrap_func);
}

absl::Status GlobRe2(
    const absl::StatusOr<RootAndPattern>& pattern,
    const GlobOptions& options,
    const GlobEntryFunc& func) {
  MBO_RETURN_IF_ERROR(pattern);
  const RE2 re2(pattern->pattern);
  if (re2.error_code() != RE2::NoError) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Could not compile re2: '%s': %s.", pattern->pattern, re2.error()));
  }
  return GlobRe2(pattern->root, re2, options, func);
}

absl::Status Glob(
    const fs::path& root,
    std::string_view pattern,
    const Glob2Re2Options& re2_convert_options,
    const GlobOptions& options,
    const GlobEntryFunc& func) {
  MBO_MOVE_TO_OR_RETURN(Glob2Re2(pattern, re2_convert_options), const std::unique_ptr<const RE2> regex);
  return GlobRe2(root, *regex, options, func);
}

absl::Status Glob(
    const absl::StatusOr<RootAndPattern>& pattern,
    const Glob2Re2Options& re2_convert_options,
    const GlobOptions& options,
    const GlobEntryFunc& func) {
  MBO_RETURN_IF_ERROR(pattern);
  return Glob(pattern->root, pattern->pattern, re2_convert_options, options, func);
}

}  // namespace mbo::file
