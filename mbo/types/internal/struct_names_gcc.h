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

#ifndef MBO_TYPES_INTERNAL_STRUCT_NAMES_GCC_H_
#define MBO_TYPES_INTERNAL_STRUCT_NAMES_GCC_H_

#if defined(__GNUC__) && !defined(__clang__)

# include <array>
# include <cstddef>
# include <memory>
# include <string_view>
# include <tuple>
# include <type_traits>
# include <utility>

# include "absl/types/span.h"                     // IWYU pragma: keep
# include "mbo/types/internal/decompose_count.h"  // IWYU pragma: keep
# include "mbo/types/tuple_extras.h"              // IWYU pragma: keep

// IWYU pragma: private, include "mbo/types/internal/struct_names.h"

namespace mbo::types::types_internal::gcc {

namespace struct_names_gcc_internal {

// This object is intentionally declared but never defined. It provides a
// compile-time address expression without constructing T. An accidental
// runtime use consequently becomes a link error.
template<typename T>
struct FakeObjectStorage {
  explicit constexpr FakeObjectStorage(const T& data) : value(data) {}

  T value;
  static FakeObjectStorage<T> instance;
};

template<typename T>
constexpr T& FakeObject() noexcept {
  return FakeObjectStorage<T>::instance.value;
}

template<typename T, std::size_t = 0>
struct AnyLvalue {
  template<typename U>
  requires(!std::same_as<U, T>)
  constexpr operator U&() const noexcept;  // NOLINT(*-explicit-*)
};

template<typename T, std::size_t = 0>
struct AnyRvalue {
  template<typename U>
  requires(!std::same_as<U, T>)
  constexpr operator U() const noexcept;  // NOLINT(*-explicit-*)
};

template<typename T, std::size_t = 0>
struct AnyLvalueNonBase {
  template<typename U>
  requires(!std::is_base_of_v<U, T> && !std::same_as<U, T>)
  constexpr operator U&() const noexcept;  // NOLINT(*-explicit-*)
};

template<typename T, std::size_t = 0>
struct AnyRvalueNonBase {
  template<typename U>
  requires(!std::is_base_of_v<U, T> && !std::same_as<U, T>)
  constexpr operator U() const noexcept;  // NOLINT(*-explicit-*)
};

template<typename T, std::size_t kArgCount>
concept AggregateConstructible =
    (kArgCount == 0 && requires { T{}; }) ||
    []<std::size_t kFirst, std::size_t... kRest>(std::index_sequence<kFirst, kRest...> /*unused*/) {
      if constexpr (std::is_copy_constructible_v<T>) {
        return requires { T{AnyLvalueNonBase<T, kFirst>(), AnyLvalue<T, kRest>()...}; };
      } else {
        return requires { T{AnyRvalueNonBase<T, kFirst>(), AnyRvalue<T, kRest>()...}; };
      }
    }(std::make_index_sequence<kArgCount>());

template<typename T, std::size_t kArgCount = 0>
requires std::is_aggregate_v<T>
consteval std::size_t CountAggregateFields() noexcept {
  if constexpr (kArgCount >= ::mbo::types::types_internal::kMaxSupportedFieldCount) {
    return ::mbo::types::types_internal::kNotDecomposableValue;
  } else if constexpr (AggregateConstructible<T, kArgCount> && !AggregateConstructible<T, kArgCount + 1>) {
    return kArgCount;
  } else {
    return CountAggregateFields<T, kArgCount + 1>();
  }
}

template<typename T>
consteval std::size_t FieldCount() noexcept {
  if constexpr (IsEmptyType<T>) {
    return 0;
  } else if constexpr (::mbo::types::CanCreateTuple<T>) {
    return ::mbo::types::types_internal::DecomposeCountImpl<T>::value;
  } else if constexpr (std::is_aggregate_v<T> && !std::is_trivially_destructible_v<T>) {
    return CountAggregateFields<T>();
  } else {
    return ::mbo::types::types_internal::kNotDecomposableValue;
  }
}

template<typename T>
constexpr auto FieldAddresses() noexcept {
  return ::mbo::types::types_internal::DecomposeHelper::ToAddressTuple<FieldCount<T>()>(FakeObject<T>());
}

template<typename T, std::size_t kIndex>
constexpr auto FieldAddress() noexcept {
  return std::get<kIndex>(FieldAddresses<T>());
}

template<auto kAddress>
using AddressAsTemplateArgument = void;

template<typename T>
consteval bool AllFieldsHaveAddresses() noexcept {
  if constexpr (FieldCount<T>() == ::mbo::types::types_internal::kNotDecomposableValue) {
    return false;
  } else {
    return std::tuple_size_v<decltype(FieldAddresses<T>())> == FieldCount<T>();
  }
}

template<typename T, auto kAddress>
consteval std::string_view FieldSignature() noexcept {
  return __PRETTY_FUNCTION__;
}

constexpr bool IsIdentifierByte(char ch) noexcept {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_'
         || static_cast<unsigned char>(ch) >= 0x80;
}

consteval std::string_view ExtractFieldName(std::string_view signature) noexcept {
  constexpr std::string_view kAddressMarker = "kAddress = ";
  const std::size_t marker = signature.rfind(kAddressMarker);
  if (marker == std::string_view::npos) {
    return {};
  }

  const std::size_t value_begin = marker + kAddressMarker.size();
  std::size_t value_end = signature.find(';', value_begin);
  if (value_end == std::string_view::npos) {
    value_end = signature.rfind(']');
  }
  if (value_end == std::string_view::npos || value_end <= value_begin) {
    return {};
  }

  while (value_end > value_begin && !IsIdentifierByte(signature[value_end - 1])) {
    --value_end;
  }
  std::size_t name_begin = value_end;
  while (name_begin > value_begin && IsIdentifierByte(signature[name_begin - 1])) {
    --name_begin;
  }
  return signature.substr(name_begin, value_end - name_begin);
}

template<typename T, std::size_t kIndex>
consteval std::string_view FieldName() noexcept {
  return ExtractFieldName(FieldSignature<T, FieldAddress<T, kIndex>()>());
}

struct ParserSentinel {
  int expected_field_name;
};

static_assert(FieldName<ParserSentinel, 0>() == "expected_field_name");

template<typename T, std::size_t kIndex>
inline constexpr auto kStoredFieldName = []() consteval {
  constexpr std::string_view kName = FieldName<T, kIndex>();
  std::array<char, kName.size() + 1> result{};
  for (std::size_t pos = 0; pos < kName.size(); ++pos) {
    result[pos] = kName[pos];
  }
  return result;
}();

template<typename T, std::size_t... kIndex>
consteval auto MakeFieldNames(std::index_sequence<kIndex...> /*unused*/) noexcept {
  return std::array<std::string_view, sizeof...(kIndex)>{
      std::string_view{kStoredFieldName<T, kIndex>.data(), kStoredFieldName<T, kIndex>.size() - 1}...,
  };
}

}  // namespace struct_names_gcc_internal

template<typename T>
concept SupportsFieldNames = std::is_class_v<T> && !std::is_array_v<T> && !std::is_union_v<T>
                             && (IsEmptyType<T> || struct_names_gcc_internal::AllFieldsHaveAddresses<T>());

template<typename T>
concept SupportsFieldNamesConstexpr = SupportsFieldNames<T>;

template<typename T, bool = SupportsFieldNames<T>>
class StructMeta final {
 public:
  static constexpr absl::Span<const std::string_view> GetFieldNames() noexcept {
    return absl::MakeConstSpan(kFieldNames);
  }

 private:
  static constexpr std::size_t kFieldCount = struct_names_gcc_internal::FieldCount<T>();
  inline static constexpr auto kFieldNames =
      struct_names_gcc_internal::MakeFieldNames<T>(std::make_index_sequence<kFieldCount>{});
};

template<typename T>
class StructMeta<T, false> final {
 public:
  static constexpr absl::Span<const std::string_view> GetFieldNames() noexcept { return {}; }
};

}  // namespace mbo::types::types_internal::gcc

#endif  // defined(__GNUC__) && !defined(__clang__)
#endif  // MBO_TYPES_INTERNAL_STRUCT_NAMES_GCC_H_
