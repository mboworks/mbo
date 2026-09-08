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

#ifndef MBO_STRINGS_CONTAINS_H_
#define MBO_STRINGS_CONTAINS_H_

#include <string_view>

namespace mbo::strings {
namespace contains_internal {

template<typename Haystack, typename Needle>
[[nodiscard]] constexpr bool Contains(Haystack haystack, Needle needle) noexcept {
  if constexpr (requires { haystack.contains(needle); }) {
    return haystack.contains(needle);
  } else {
    return haystack.find(needle) != Haystack::npos;  // NOLINT(abseil-string-find-str-contains)
  }
}

}  // namespace contains_internal

// `constexpr` counterparts to `absl::StrContains`.
//
// `absl::StrContains` is not usable in a constant expression, so constexpr code -
// `mbo::types::tstring`, compile-time tables - cannot call it and has to spell out
// `haystack.find(needle) != npos`. That form reads worse and is what
// `abseil-string-find-str-contains` flags, with a fix that would not compile here.
// These have the same meaning and are constexpr, so the check is satisfied without
// giving up compile-time evaluation.
//
// These wrappers preserve mbo's existing public API while remaining usable
// with C++20. `std::string_view::find` is constexpr and has the same required
// semantics; newer standard libraries provide an equivalent `std::string_view::contains`.
//
// This header deliberately has no dependencies beyond <string_view>, so that
// `mbo/types` can use it without creating a cycle (`mbo/strings` depends on
// `mbo/types:traits_cc`).
//
// Semantics match `absl::StrContains` exactly, including the edge cases:
//   * An EMPTY needle is contained in everything - `Contains(str, "")` is true for
//     any `str`, including an empty one, because `find` returns 0 rather than npos.
//   * An empty haystack contains only an empty needle.
//   * The `char` overload looks for that character, so `Contains(str, '\0')` asks
//     whether `str` holds an embedded NUL - it does not mean "empty needle".

[[nodiscard]] constexpr bool Contains(std::string_view haystack, std::string_view needle) noexcept {
  return contains_internal::Contains(haystack, needle);
}

[[nodiscard]] constexpr bool Contains(std::string_view haystack, char needle) noexcept {
  return contains_internal::Contains(haystack, needle);
}

}  // namespace mbo::strings

#endif  // MBO_STRINGS_CONTAINS_H_
