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

#ifndef MBO_TYPES_REQUIRED_H_
#define MBO_TYPES_REQUIRED_H_

#include <compare>   // IWYU pragma: keep
#include <concepts>  // IWYU pragma: keep
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/hash/hash.h"
#include "absl/strings/str_format.h"
#include "mbo/types/traits.h"  // IWYU pragma: keep

namespace mbo::types {

template<typename T>
class Required;

namespace types_internal {

template<typename T>
struct IsRequired : std::false_type {};

template<typename T>
struct IsRequired<Required<T>> : std::true_type {};

}  // namespace types_internal

// Template class `Required<T>` is a wrapper around a type `T`. The value can be
// replaced without using assign or move-assign operators. Instead the wrapper
// will call the destructor and then in-place construct the wrapped value using
// the type's move constructor.
//
// In other words the reason for this type is the rare case where a variable of
// a type `T` needs to be assigned (say for a test) while the type follows the
// rule of 0/3/5 but only allows move-construction.
//
// The type is similar to `RefWrap` and `std::optional` (only it cannot be
// `std::nullopt`).
//
// The wrapper is not implemented itself as a wrapper or specialization of STL
// type `std::optional` due to its overhead.
//
// `emplace` constructs its replacement before destroying the current value.
// Since `Required` has no empty state, replacement is available only when `T`
// can then be moved into place without throwing.
//
// Example:
// ```
// template<typename T>
// struct MoveOnly {
//  ~MoveOnly() = default;
//  MoveOnly() = default;
//  MoveOnly(T data) : data(std::move(data)) {}
//
//  MoveOnly(const MoveOnly&) = delete;
//  MoveOnly& operator=(const MoveOnly&) = delete;
//  MoveOnly(MoveOnly&&) noexcept = default;
//  MoveOnly& operator=(const MoveOnly&&) = delete;
//
//  const T data;
// };
//
// struct User {
//   Required<MoveOnly<std::string>> data;
// };
//
// User user("foo");
//
// // If `User.data` was a plain `MoveOnly` type, then data would not be assignable.
// // However, it is wrapper as a `Required` which guarantees initialized presence,
// // and allows updating via assign.
//
// user.data.emplace("bar");
// ```
template<typename T>
class Required {
 public:
  using type = T;

  Required() = default;

  constexpr explicit Required(T v) : value_(std::move(v)) {}

  template<typename U>
  requires(ConstructibleFrom<T, U> && !std::same_as<T, U>)
  constexpr explicit Required(U&& v) : value_(std::forward<U>(v)) {}

  template<typename... Args>
  requires(ConstructibleFrom<T, Args...>)
  constexpr explicit Required(std::in_place_t /*unused*/, Args&&... args) : value_(std::forward<Args>(args)...) {}

  constexpr Required& emplace(T v) noexcept(std::is_nothrow_move_constructible_v<T>)  // NOLINT(*-identifier-naming)
  requires(std::is_nothrow_move_constructible_v<T>) {
    std::destroy_at(std::addressof(value_));
    std::construct_at(std::addressof(value_), std::move(v));
    return *this;
  }

  template<typename... Args>
  requires(ConstructibleFrom<T, Args...> && std::is_nothrow_move_constructible_v<T>)
  constexpr Required& emplace(Args&&... args)  // NOLINT(readability-identifier-naming)
      noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    T replacement(std::forward<Args>(args)...);
    std::destroy_at(std::addressof(value_));
    std::construct_at(std::addressof(value_), std::move(replacement));
    return *this;
  }

  constexpr T& get() noexcept { return value_; }  // NOLINT(*-identifier-naming)

  constexpr const T& get() const noexcept { return value_; }  // NOLINT(*-identifier-naming)

  constexpr T* operator->() noexcept __attribute__((returns_nonnull)) { return &value_; }

  constexpr const T* operator->() const noexcept __attribute__((returns_nonnull)) { return &value_; }

  constexpr T& operator*() noexcept { return value_; }

  constexpr const T& operator*() const noexcept { return value_; }

  constexpr operator T&() noexcept { return value_; }  // NOLINT(*-explicit-*)

  constexpr operator const T&() const noexcept { return value_; }  // NOLINT(*-explicit-*)

  template<typename U>
  requires(std::three_way_comparable_with<T, U>)
  constexpr auto operator<=>(const Required<U>& other) const noexcept(noexcept(value_ <=> *other)) {
    return value_ <=> *other;
  }

  template<typename U>
  requires(!types_internal::IsRequired<std::remove_cvref_t<U>>::value && std::three_way_comparable_with<T, U>)
  constexpr auto operator<=>(const U& other) const noexcept(noexcept(value_ <=> other)) {
    return value_ <=> other;
  }

  template<typename H>
  friend H AbslHashValue(H hash, const Required<T>& v) {
    return H::combine(std::move(hash), v.value_);
  }

  template<typename Sink>
  friend void AbslStringify(Sink& sink, const Required<T>& v) {
    absl::Format(&sink, "%v", v.value_);
  }

 private:
  T value_;
};

}  // namespace mbo::types

#endif  // MBO_TYPES_REQUIRED_H_
