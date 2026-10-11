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

#ifndef MBO_TYPES_OPTIONAL_REF_H_
#define MBO_TYPES_OPTIONAL_REF_H_

#include <compare>
#include <concepts>  // IWYU pragma: keep
#include <optional>
#include <type_traits>
#include <utility>

#include "absl/hash/hash.h"
#include "absl/strings/str_format.h"
#include "mbo/config/config.h"
#include "mbo/config/require.h"
#include "mbo/log/demangle.h"

namespace mbo::types {

template<typename Rhs, typename Lhs>
concept AssignableTo = std::assignable_from<Lhs, Rhs>;

// An optional non-owning reference with explicit rebinding.
//
// Unlike `std::optional<std::reference_wrapper<T>>`, assigning a value writes through the
// referenced object; it does not rebind the reference. Use `set_ref()` to rebind and `reset()` to
// disengage. Copy and move assignment are deleted so rebinding cannot happen implicitly. Use
// `OptionalRef<const T>` for an immutable referenced object.
//
// Example:
//
// ```c++
// int first = 1;
// int second = 2;
// OptionalRef<int> ref = first;
// ref = 3;             // Assigns first; ref still refers to first.
// ref.set_ref(second);  // Explicitly rebinds ref to second.
// ref.reset();          // Disengages ref.
// ```
template<typename T>
requires(!std::is_reference_v<T>)
class OptionalRef {
 public:
  using value_type = T&;             // NOLINT(*-identifier-naming)
  using reference = T&;              // NOLINT(*-identifier-naming)
  using const_reference = const T&;  // NOLINT(*-identifier-naming)
  using pointer = T*;                // NOLINT(*-identifier-naming)
  using const_pointer = const T*;    // NOLINT(*-identifier-naming)

  constexpr ~OptionalRef() noexcept = default;

  constexpr OptionalRef() noexcept = default;

  constexpr OptionalRef(reference v) : v_(&v) {}  // NOLINT(*-explicit-*)

  constexpr OptionalRef(std::nullopt_t /*unused */) : v_(nullptr) {}  // NOLINT(*-explicit-*)

  constexpr OptionalRef(const OptionalRef&) noexcept = default;
  constexpr OptionalRef& operator=(const OptionalRef&) = delete;
  constexpr OptionalRef(OptionalRef&&) noexcept = default;
  constexpr OptionalRef& operator=(OptionalRef&&) = delete;

  constexpr OptionalRef& reset() noexcept {  // NOLINT(*-identifier-naming)
    v_ = nullptr;
    return *this;
  }

  constexpr OptionalRef& set_ref(T& v) noexcept {  // NOLINT(*-identifier-naming)
    v_ = &v;
    return *this;
  }

  constexpr bool has_value() const noexcept { return v_ != nullptr; }  // NOLINT(*-identifier-naming)

  constexpr explicit operator bool() const noexcept { return has_value(); }

  constexpr reference value() noexcept(!config::kRequireThrows) {  // NOLINT(*-identifier-naming)
    MBO_CONFIG_REQUIRE(has_value(), "No value set for: ") << mbo::log::DemangleV(*this);
    return *v_;
  }

  constexpr const_reference value() const noexcept(!config::kRequireThrows) {  // NOLINT(*-identifier-naming)
    MBO_CONFIG_REQUIRE(has_value(), "No value set for: ") << mbo::log::DemangleV(*this);
    return *v_;
  }

  constexpr reference operator*() noexcept(!config::kRequireThrows) { return value(); }

  constexpr const_reference operator*() const noexcept(!config::kRequireThrows) { return value(); }

  constexpr pointer operator->() noexcept(!config::kRequireThrows) { return &value(); }

  constexpr const_pointer operator->() const noexcept(!config::kRequireThrows) { return &value(); }

  template<AssignableTo<T&> U = T>
  constexpr OptionalRef& operator=(U&& v) noexcept(!config::kRequireThrows && std::is_nothrow_assignable_v<T&, U&&>) {
    value() = std::forward<U>(v);
    return *this;
  }

  template<AssignableTo<T&> U = T>
  constexpr OptionalRef& operator=(const U& v) noexcept(
      !config::kRequireThrows && std::is_nothrow_assignable_v<T&, const U&>) {
    value() = v;
    return *this;
  }

  template<std::equality_comparable_with<T> U = T>
  constexpr bool operator==(const OptionalRef<U>& rhs) const noexcept {
    if (has_value() != rhs.has_value()) {
      return false;
    }
    if (!has_value()) {
      return true;
    }
    return value() == rhs.value();
  }

  template<std::totally_ordered_with<T> U = T>
  constexpr bool operator<(const OptionalRef<U>& rhs) const noexcept {
    if (has_value() != rhs.has_value()) {
      return !has_value();
    }
    if (!has_value()) {
      return false;
    }
    return value() < rhs.value();
  }

  template<std::three_way_comparable_with<T> U = T>
  constexpr auto operator<=>(const OptionalRef<U>& rhs) const {
    using Result = decltype(std::declval<T>() <=> std::declval<T>());
    if (Result result = has_value() <=> rhs.has_value(); result != Result::equal) {
      return result;
    }
    if (!has_value()) {
      return Result::equal;
    }
    return value() <=> rhs.value();
  }

  constexpr bool operator==(std::nullopt_t /*unused*/) const noexcept { return v_ == nullptr; }

  constexpr bool operator<(std::nullopt_t /*unused*/) const noexcept { return v_ < nullptr; }

  constexpr std::strong_ordering operator<=>(std::nullopt_t /*unused*/) const noexcept {
    if (!has_value()) {
      return std::strong_ordering::equal;
    }
    return std::strong_ordering::greater;
  }

  template<typename U = T>
  requires(!std::same_as<U, OptionalRef<T>> && std::equality_comparable_with<T, U>)
  constexpr bool operator==(const U& other) const noexcept {
    if (!has_value()) {
      return false;
    }
    return value() == other;
  }

  template<typename U = T>
  requires(!std::same_as<U, OptionalRef<T>> && std::totally_ordered_with<T, U>)
  constexpr bool operator<(const U& other) const noexcept {
    if (!has_value()) {
      return true;
    }
    return value() < other;
  }

  template<typename U = T>
  requires(!std::same_as<U, OptionalRef<T>> && std::three_way_comparable_with<T, U>)
  constexpr auto operator<=>(const U& other) const noexcept {
    using Result = decltype(*v_ <=> other);
    if (!has_value()) {
      return Result::less;
    }
    return value() <=> other;
  }

  template<typename H>
  friend constexpr H AbslHashValue(H hash, const OptionalRef<T>& v) {
    if (v.has_value()) {
      return H::combine(std::move(hash), true, v.value());
    } else {
      return H::combine(std::move(hash), false);
    }
  }

  template<typename Sink>
  friend constexpr void AbslStringify(Sink& sink, const OptionalRef<T>& v) {
    if (v.has_value()) {
      absl::Format(&sink, "%v", v.value());
    } else {
      absl::Format(&sink, "std::nullopt");
    }
  }

 private:
  T* v_ = nullptr;
};

template<typename T>
concept IsOptionalRef = requires {
  typename T::value_type;
  typename T::reference;
  requires std::same_as<T, OptionalRef<std::remove_reference_t<typename T::value_type>>>;
};

}  // namespace mbo::types

#endif  // MBO_TYPES_OPTIONAL_REF_H_
