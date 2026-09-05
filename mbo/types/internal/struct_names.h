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

#ifndef MBO_TYPES_INTERNAL_STRUCT_NAMES_H_
#define MBO_TYPES_INTERNAL_STRUCT_NAMES_H_

#include "absl/types/span.h"  // IWYU pragma: keep

// IWYU pragma: private, include "mbo/types/extender.h"

#if defined(__clang__) && __has_builtin(__builtin_dump_struct)

# include "mbo/types/internal/struct_names_clang.h"  // IWYU pragma: keep

namespace mbo::types::types_internal {

static constexpr bool kStructNameSupport = true;

template<typename T>
concept SupportsFieldNames = clang::SupportsFieldNames<T>;
template<typename T>
concept SupportsFieldNamesConstexpr = clang::SupportsFieldNamesConstexpr<T>;

template<typename T>
inline constexpr absl::Span<const std::string_view> GetFieldNames() {
  return clang::StructMeta<T>::GetFieldNames();
}

}  // namespace mbo::types::types_internal

#elif defined(__GNUC__) && !defined(__clang__)

# include "mbo/types/internal/struct_names_gcc.h"  // IWYU pragma: keep

namespace mbo::types::types_internal {

static constexpr bool kStructNameSupport = true;

template<typename T>
concept SupportsFieldNames = gcc::SupportsFieldNames<T>;
template<typename T>
concept SupportsFieldNamesConstexpr = gcc::SupportsFieldNamesConstexpr<T>;

template<typename T>
inline constexpr absl::Span<const std::string_view> GetFieldNames() {
  return gcc::StructMeta<T>::GetFieldNames();
}

}  // namespace mbo::types::types_internal

#else  // __clang__

namespace mbo::types::types_internal {

static const bool kStructNameSupport = false;

template<typename T>
concept SupportsFieldNames = false;
template<typename T>
concept SupportsFieldNamesConstexpr = false;

template<typename T>
inline constexpr absl::Span<const std::string_view> GetFieldNames() {
  return {};
}

}  // namespace mbo::types::types_internal

#endif  // __clang__
#endif  // MBO_TYPES_INTERNAL_STRUCT_NAMES_H_
