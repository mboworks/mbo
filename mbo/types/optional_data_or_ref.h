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

#ifndef MBO_TYPES_OPTIONAL_DATA_OR_REF_H_
#define MBO_TYPES_OPTIONAL_DATA_OR_REF_H_

#include <compare>
#include <concepts>  // IWYU pragma: keep
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "absl/hash/hash.h"
#include "absl/strings/str_format.h"
#include "mbo/config/require.h"
#include "mbo/log/demangle.h"
#include "mbo/types/traits.h"  // IWYU pragma: keep

namespace mbo::types {

// NOLINTBEGIN(*-identifier-naming)

template<typename T, typename RefT = T>
requires(
    !std::is_reference_v<T> && !std::is_reference_v<RefT>
    && std::same_as<std::remove_cvref_t<T>, std::remove_cvref_t<RefT>>)
class OptionalDataOrRef {
 public:
  using value_type = T;
  using pointer = RefT*;
  using const_pointer = const RefT*;
  using reference = RefT&;
  using const_reference = const RefT&;

  constexpr ~OptionalDataOrRef() = default;

  // LCOV_MERGE_FUNC_LINE: repeated for every stored/reference type pair.
  constexpr OptionalDataOrRef() noexcept = default;

  constexpr OptionalDataOrRef(std::nullopt_t /* unused */) noexcept {}  // NOLINT(*-explicit-*)

  template<typename U = T>
  requires(!std::same_as<U, OptionalDataOrRef>)
  constexpr OptionalDataOrRef(T&& v) noexcept(std::is_nothrow_move_constructible_v<T>)  // NOLINT(*-explicit-*)
      : data_(std::in_place_index<kDataIndex>, std::move(v)) {}

  constexpr OptionalDataOrRef(RefT& v) noexcept  // NOLINT(*-explicit-*)
  requires(!std::same_as<T, RefT>)
      : data_(std::in_place_index<kRefIndex>, std::ref(v)) {}

  template<typename U = RefT>
  requires(
      !std::is_rvalue_reference_v<U> && !std::same_as<U, OptionalDataOrRef>
      && (!std::is_const_v<U> || std::is_const_v<RefT>))
  constexpr OptionalDataOrRef(U& v) noexcept  // NOLINT(*-explicit-*)
      : data_(std::in_place_index<kRefIndex>, std::ref(static_cast<RefT&>(v))) {}

  constexpr OptionalDataOrRef(const OptionalDataOrRef& other) noexcept(std::is_nothrow_copy_constructible_v<T>)
  requires std::copy_constructible<T>
  {
    CopyFrom(other);
  }

  constexpr OptionalDataOrRef& operator=(const OptionalDataOrRef& other) noexcept(
      std::is_nothrow_copy_constructible_v<T>)
  requires std::copy_constructible<T>
  {
    if (this == &other) {
      return *this;
    }
    CopyFrom(other);
    return *this;
  }

  constexpr OptionalDataOrRef(OptionalDataOrRef&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
  requires std::move_constructible<T>
  {
    MoveFrom(other);
  }

  constexpr OptionalDataOrRef& operator=(OptionalDataOrRef&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
  requires std::move_constructible<T>
  {
    if (this != &other) {
      MoveFrom(other);
    }
    return *this;
  }

  constexpr OptionalDataOrRef& operator=(std::nullopt_t /* unused */) noexcept {
    reset();
    return *this;
  }

  constexpr OptionalDataOrRef& operator=(value_type&& v) noexcept(std::is_nothrow_move_constructible_v<T>) {
    if (HoldsData() && std::addressof(v) == std::addressof(Data())) {
      return *this;
    }
    emplace(std::move(v));
    return *this;
  }

  template<typename U>
  requires(std::assignable_from<T&, U> && ConstructibleFrom<T, U> && !std::same_as<U, T>)
  constexpr OptionalDataOrRef& operator=(U&& v) noexcept(
      std::is_nothrow_constructible_v<T, U>
      && (std::is_rvalue_reference_v<U&&> || std::is_nothrow_assignable_v<T&, U>)) {
    if constexpr (std::is_rvalue_reference_v<decltype(v)>) {  // NOLINT(*-branch-clone)
      emplace(std::forward<U>(v));
    } else if (has_value()) {
      value() = std::forward<U>(v);
    } else {
      emplace(std::forward<U>(v));
    }
    return *this;
  }

  constexpr OptionalDataOrRef& reset() noexcept {
    data_.template emplace<kNullIndex>();
    return *this;
  }

  // LCOV_MERGE_FUNC_LINE: repeated for every stored/reference type pair.
  constexpr OptionalDataOrRef& set_ref(reference v) noexcept {
    data_.template emplace<kRefIndex>(std::ref(v));
    return *this;
  }

  template<typename... Args>
  // LCOV_MERGE_FUNC_LINE: repeated for every stored/reference type pair.
  constexpr OptionalDataOrRef& emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
  requires(ConstructibleFrom<T, Args...>)
  {
    // Make the public state empty before construction so a throwing replacement
    // cannot leave the previous alternative's lifetime ambiguous.
    reset();
    data_.template emplace<kDataIndex>(std::forward<Args>(args)...);
    return *this;
  }

  constexpr explicit operator bool() const noexcept { return has_value(); }

  constexpr bool has_value() const noexcept { return HoldsData() || HoldsReference(); }

  constexpr bool HoldsData() const noexcept { return data_.index() == kDataIndex; }

  constexpr bool HoldsNullopt() const noexcept { return !has_value(); }

  constexpr bool HoldsReference() const noexcept { return data_.index() == kRefIndex; }

  constexpr reference value() noexcept {
    MBO_CONFIG_REQUIRE(has_value(), "No value set for: ") << mbo::log::DemangleV(*this);
    return HoldsData() ? Data() : Reference();
  }

  // LCOV_MERGE_FUNC_LINE: repeated for every stored/reference type pair.
  constexpr const_reference value() const noexcept {
    MBO_CONFIG_REQUIRE(has_value(), "No value set for: ") << mbo::log::DemangleV(*this);
    return HoldsData() ? Data() : Reference();
  }

  // Returns `value()` if `holds_value()` is true, a reference to static defaults otherwise.
  constexpr const_reference get() const noexcept
  requires std::is_default_constructible_v<T>
  {
    static constexpr T kDefaults{};
    return has_value() ? value() : kDefaults;
  }

  // Returns `value()` if `holds_value()` is true, a reference `defaults`.
  // BEWARE of dangling references: returning the caller's own reference is the
  // point of this API, so the caller owns the lifetime question. The suppression
  // must sit on the line immediately above the declaration - a rationale placed
  // between it and the declaration would bind it to the comment instead.
  // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
  constexpr const_reference get(const T& defaults) const noexcept { return has_value() ? value() : defaults; }

  // Returns a reference to existing data or created data. If the object:
  // * is `std::nullopt`, then a default value will be emplace and is reference returned.
  // * contains a value, then its reference will be returned.
  // * contains a reference, then that reference is emplace and then its reference returned.
  template<typename... Args>
  constexpr value_type& as_data(Args&&... args) noexcept(
      std::is_nothrow_constructible_v<T, reference> && std::is_nothrow_constructible_v<T, Args...>)
  requires(ConstructibleFrom<T, reference> && ConstructibleFrom<T, Args...>)
  {
    if (!HoldsData()) {
      if (HoldsReference()) {
        emplace(Reference());
      } else {
        emplace(std::forward<Args>(args)...);
      }
    }
    return Data();
  }

  constexpr reference operator*() noexcept { return value(); }

  constexpr const_reference operator*() const noexcept { return value(); }

  constexpr pointer operator->() noexcept { return &value(); }

  constexpr const_pointer operator->() const noexcept { return &value(); }

  template<std::equality_comparable_with<T> U = T, typename RefU = U>
  constexpr bool operator==(const OptionalDataOrRef<U, RefU>& rhs) const
      noexcept(noexcept(std::declval<const T&>() == std::declval<const U&>())) {
    if (has_value() != rhs.has_value()) {
      return false;
    }
    if (!has_value()) {
      return true;
    }
    return value() == rhs.value();
  }

  template<std::totally_ordered_with<T> U = T, typename RefU = U>
  constexpr bool operator<(const OptionalDataOrRef<U, RefU>& rhs) const
      noexcept(noexcept(std::declval<const T&>() < std::declval<const U&>())) {
    if (has_value() != rhs.has_value()) {
      return !has_value();
    }
    if (!has_value()) {
      return false;
    }
    return value() < rhs.value();
  }

  template<std::three_way_comparable_with<T> U = T, typename RefU = U>
  constexpr auto operator<=>(const OptionalDataOrRef<U, RefU>& rhs) const
      noexcept(noexcept(std::declval<const T&>() <=> std::declval<const U&>())) {
    using Result = decltype(std::declval<const T&>() <=> std::declval<const U&>());
    if (Result result = has_value() <=> rhs.has_value(); result != Result::equal) {
      return result;
    }
    if (!has_value()) {
      return Result::equal;
    }
    return value() <=> rhs.value();
  }

  constexpr bool operator==(std::nullopt_t /*unused*/) const noexcept { return !has_value(); }

  constexpr bool operator<(std::nullopt_t /*unused*/) const noexcept { return false; }

  constexpr std::strong_ordering operator<=>(std::nullopt_t /*unused*/) const noexcept {
    if (!has_value()) {
      return std::strong_ordering::equal;
    }
    return std::strong_ordering::greater;
  }

  template<typename U = T>
  requires(!std::same_as<U, OptionalDataOrRef<T, RefT>> && std::equality_comparable_with<T, U>)
  constexpr bool operator==(const U& other) const noexcept(noexcept(std::declval<const T&>() == other)) {
    if (!has_value()) {
      return false;
    }
    return value() == other;
  }

  template<typename U = T>
  requires(!std::same_as<U, OptionalDataOrRef<T, RefT>> && std::totally_ordered_with<T, U>)
  constexpr bool operator<(const U& other) const noexcept(noexcept(std::declval<const T&>() < other)) {
    if (!has_value()) {
      return true;
    }
    return value() < other;
  }

  template<typename U = T>
  requires(!std::same_as<U, OptionalDataOrRef<T, RefT>> && std::three_way_comparable_with<T, U>)
  constexpr auto operator<=>(const U& other) const noexcept(noexcept(std::declval<const T&>() <=> other)) {
    using Result = decltype(std::declval<const T&>() <=> other);
    if (!has_value()) {
      return Result::less;
    }
    return value() <=> other;
  }

  template<typename H>
  friend constexpr H AbslHashValue(H hash, const OptionalDataOrRef<T, RefT>& v) {
    if (v.has_value()) {
      return H::combine(std::move(hash), true, v.value());
    } else {
      return H::combine(std::move(hash), false);
    }
  }

  template<typename Sink>
  // LCOV_MERGE_FUNC_LINE: repeated for every sink and stored/reference type pair.
  friend constexpr void AbslStringify(Sink& sink, const OptionalDataOrRef<T, RefT>& v) {
    if (v.has_value()) {
      absl::Format(&sink, "%v", v.value());
    } else {
      absl::Format(&sink, "std::nullopt");
    }
  }

 private:
  static constexpr std::size_t kNullIndex = 0;
  static constexpr std::size_t kDataIndex = 1;
  static constexpr std::size_t kRefIndex = 2;

  constexpr T& Data() noexcept { return std::get<kDataIndex>(data_); }

  // LCOV_MERGE_FUNC_LINE: repeated for every stored/reference type pair.
  constexpr const T& Data() const noexcept { return std::get<kDataIndex>(data_); }

  // LCOV_MERGE_FUNC_LINE: repeated for every stored/reference type pair.
  constexpr reference Reference() const noexcept { return std::get<kRefIndex>(data_).get(); }

  constexpr void CopyFrom(const OptionalDataOrRef& other) noexcept(std::is_nothrow_copy_constructible_v<T>)
  requires std::copy_constructible<T>
  {
    if (other.HoldsData()) {
      emplace(other.Data());
    } else if (other.HoldsReference()) {
      set_ref(other.Reference());
    } else {
      reset();
    }
  }

  constexpr void MoveFrom(OptionalDataOrRef& other) noexcept(std::is_nothrow_move_constructible_v<T>)
  requires std::move_constructible<T>
  {
    if (other.HoldsData()) {
      emplace(std::move(other.Data()));
      other.reset();
    } else if (other.HoldsReference()) {
      set_ref(other.Reference());
    } else {
      reset();
    }
  }

  std::variant<std::monostate, T, std::reference_wrapper<RefT>> data_;
};

template<typename T>
using OptionalDataOrConstRef = OptionalDataOrRef<T, const T>;

template<typename T>
concept IsOptionalDataOrRef = requires {
  typename T::value_type;
  typename T::reference;
  requires std::same_as<T, OptionalDataOrRef<typename T::value_type, std::remove_reference_t<typename T::reference>>>;
};

// NOLINTEND(*-identifier-naming)

}  // namespace mbo::types

#endif  // MBO_TYPES_OPTIONAL_DATA_OR_REF_H_
