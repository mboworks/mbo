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

#ifndef MBO_TYPES_INTERNAL_STRUCT_NAMES_CLANG_H_
#define MBO_TYPES_INTERNAL_STRUCT_NAMES_CLANG_H_

#if defined(__clang__) && __has_builtin(__builtin_dump_struct)

# include <array>
# include <atomic>  // IWYU pragma: keep
# include <cstring>
# include <string_view>
# include <type_traits>

# include "absl/types/span.h"                     // IWYU pragma: keep
# include "mbo/types/internal/decompose_count.h"  // IWYU pragma: keep
# include "mbo/types/tuple_extras.h"              // IWYU pragma: keep

// IWYU pragma: private, include "mbo/types/internal/struct_names.h"

namespace mbo::types::types_internal::clang {

inline constexpr std::size_t kMaxFieldCount = 50;

template<typename T>
concept SupportsFieldNames =
    std::is_class_v<T> && !std::is_array_v<T>  // Minimum requirement
    && std::is_destructible_v<T>  // Type T must have a constexpr destructor. But `std::is_literal_type` is deprecated,
    && !::mbo::types::HasUnionMember<T>;  // Unions are problematic as they do not use the same fields (and thus names).

template<typename T>
concept SupportsFieldNamesConstexpr =      // Constexpr capability is more restrictive.
    SupportsFieldNames<T>                  // All general requirements, plus:
    && std::is_default_constructible_v<T>  // Warning will use actual uninitialized memory when retrieving field names.
    && __is_literal_type(T);               // so the internal version has to be used.

template<typename T, bool, bool>
class StructMeta;

template<typename T>
class StructMetaBase {
 private:
  using RawT = std::remove_const_t<T>;
  template<typename U, bool, bool>
  friend class StructMeta;

  // NOLINTBEGIN(*-pro-type-union-access): Storage IS a union - it holds either
  // raw bytes or a T, so that a non-default-constructible T can be inspected
  // without ever constructing one. Reading the active member is the mechanism.
  class Storage final {
   private:
    union Uninitialized {
      constexpr Uninitialized() noexcept {}

      constexpr ~Uninitialized() noexcept {}

      Uninitialized(const Uninitialized&) = delete;
      Uninitialized& operator=(const Uninitialized&) = delete;
      Uninitialized(Uninitialized&&) = delete;
      Uninitialized& operator=(Uninitialized&&) = delete;

      char temp[sizeof(T)] = {0};  // NOLINT(*-avoid-c-arrays)
      RawT value;
    };

   public:
    constexpr Storage() noexcept {
      if constexpr (std::is_default_constructible_v<RawT>) {
        std::construct_at(&storage_.value);
      } else {
        memset(&storage_.temp[0], 0, sizeof(T));
      }
    }

    constexpr ~Storage() noexcept {
      if constexpr (std::is_default_constructible_v<T>) {
        std::destroy_at(&storage_.value);
      }
    }

    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    Storage(Storage&&) = delete;
    Storage& operator=(Storage&&) = delete;

    constexpr const T& Get() const noexcept { return storage_.value; }

   private:
    Uninitialized storage_;
  };

  // NOLINTEND(*-pro-type-union-access)

  static constexpr int DumpStructFieldCounter(
      std::size_t& field_index,
      std::string_view format,
      std::string_view indent = {},
      std::string_view /*type*/ = {},
      std::string_view /*name*/ = {},
      ...) {
    if (field_index < kMaxFieldCount && format.starts_with("%s%s %s =") && indent == "  ") {
      ++field_index;
    }
    return 0;
  }

  static constexpr int FieldCount(const T* ptr, std::size_t& field_index) {
    __builtin_dump_struct(ptr, &DumpStructFieldCounter, field_index);  // NOLINT(*-vararg)
    return 0;
  }

  static constexpr std::size_t ComputeFieldCount() {
    // NOLINTNEXTLINE(misc-const-correctness): passed to FieldCount() by non-const reference.
    std::size_t field_index{0};
    if constexpr (!IsEmptyType<T>) {
      // NOLINTNEXTLINE(misc-const-correctness): `&storage.Get()` needs a mutable object.
      Storage storage{};
      FieldCount(&storage.Get(), field_index);
    }
    return field_index;
  }
};

template<typename T, bool = SupportsFieldNames<T> && !IsEmptyType<T>, bool = SupportsFieldNamesConstexpr<T>>
class StructMeta final {
 public:
  static constexpr absl::Span<const std::string_view> GetFieldNames() {
    if constexpr (IsEmptyType<T>) {
      return {};
    } else {
      return absl::MakeSpan(kFieldNames);
    }
  }

 private:
  static constexpr std::size_t kDecomposeCount = DecomposeCountImpl<T>::value;

  struct FieldInfo {
    std::string_view name;
    std::string_view type;
  };

  static constexpr std::size_t kFieldCount =
      DecomposeCondition<T> ? kDecomposeCount : StructMetaBase<T>::ComputeFieldCount();

  using FieldData = std::array<FieldInfo, kFieldCount>;

  static constexpr int DumpStructVisitor(
      FieldData& fields,
      std::size_t& field_index,
      std::string_view format,
      std::string_view indent = {},
      std::string_view type = {},
      std::string_view name = {},
      ...) {
    if (field_index < fields.size() && format.starts_with("%s%s %s =") && indent == "  ") {
      fields.at(field_index++) = {
          .name = name,
          .type = type,
      };
    }
    return 0;
  }

  static constexpr void Init(const T* ptr, FieldData& fields, std::size_t& field_index) {
    __builtin_dump_struct(ptr, &DumpStructVisitor, fields, field_index);  // NOLINT(*-vararg)
  }

  // Older compilers may not allow this to be a `constexpr`.
  inline static constexpr FieldData kFieldData = [] consteval {
    FieldData fields;
    if constexpr (!IsEmptyType<T>) {
      // NOLINTNEXTLINE(misc-const-correctness): `&storage.Get()` needs a mutable object.
      typename StructMetaBase<T>::Storage storage{};
      std::size_t field_index = 0;
      Init(&storage.Get(), fields, field_index);
    }
    return fields;
  }();

  inline static constexpr std::array<std::string_view, kFieldCount> kFieldNames = [] consteval {
    std::array<std::string_view, kFieldCount> data;
    if constexpr (!IsEmptyType<T>) {
      for (std::size_t pos = 0; pos < kFieldCount; ++pos) {
        data.at(pos) = kFieldData.at(pos).name;
      }
    }
    return data;
  }();
};

// Support for non literal types requires runtime operation.
template<typename T>
class StructMeta<T, true, false> final {
 public:
  static absl::Span<const std::string_view> GetFieldNames() {
    if constexpr (IsEmptyType<T>) {
      return {};
    } else {
      return absl::MakeSpan(kFieldNames);
    }
  }

 private:
  static constexpr std::size_t kDecomposeCount = DecomposeCountImpl<T>::value;

  struct FieldInfo {
    std::string_view name;
    std::string_view type;
  };

  static constexpr std::size_t kFieldCount =
      DecomposeCondition<T> ? kDecomposeCount : StructMetaBase<T>::ComputeFieldCount();

  using FieldData = std::array<FieldInfo, kFieldCount>;

  static int DumpStructVisitor(
      FieldData& fields,
      std::size_t& field_index,
      std::string_view format,
      std::string_view indent = {},
      std::string_view type = {},
      std::string_view name = {},
      ...) noexcept {
    if (field_index < fields.size() && format.starts_with("%s%s %s =") && indent == "  ") {
      fields.at(field_index++) = {
          .name = name,
          .type = type,
      };
    }
    return 0;
  }

  static void Init(const T* ptr, FieldData& fields, std::size_t& field_index) noexcept {
    __builtin_dump_struct(ptr, &DumpStructVisitor, fields, field_index);  // NOLINT(*-vararg)
  }

  // Older compilers may not allow this to be a `constexpr`.
  inline static const FieldData kFieldData = [] {
    FieldData fields;
    if constexpr (!IsEmptyType<T>) {
      // NOLINTNEXTLINE(misc-const-correctness): `&storage.Get()` needs a mutable object.
      typename StructMetaBase<T>::Storage storage{};
      std::size_t field_index = 0;
      Init(&storage.Get(), fields, field_index);
    }
    return fields;
  }();

  inline static const auto kFieldNames = [] {
    std::array<std::string_view, kFieldCount> data;
    if constexpr (!IsEmptyType<T>) {
# if __has_feature(address_sanitizer)
      // In ASAN mode we need to carefully copy the data. Using atomic indexing does it.
      std::atomic_size_t pos = 0;
# else
      std::size_t pos = 0;
# endif
      for (; pos < kFieldCount; ++pos) {
        data.at(pos) = kFieldData.at(pos).name;
      }
    }
    return data;
  }();
};

// No support possible.
template<typename T>
class StructMeta<T, false, false> final {
 public:
  static constexpr absl::Span<const std::string_view> GetFieldNames() { return {}; }

  static constexpr absl::Span<const std::string_view> GetFieldTypes() { return {}; }
};

}  // namespace mbo::types::types_internal::clang

#endif  // __clang__
#endif  // MBO_TYPES_INTERNAL_STRUCT_NAMES_CLANG_H_
