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

#ifndef MBO_TYPES_OPAQUE_H_
#define MBO_TYPES_OPAQUE_H_

#include <compare>   // IWYU pragma: keep
#include <concepts>  // IWYU pragma: keep
#include <cstddef>
#include <memory>
#include <type_traits>

#include "absl/hash/hash.h"
#include "mbo/types/container_proxy.h"  // IWYU pragma: export
#include "mbo/types/traits.h"           // IWYU pragma: keep

namespace mbo::types {
namespace types_internal {

template<typename T>
struct OpaquePtrDeleter {
  void operator()(T* ptr) { OpaquePtrDeleterHook(ptr); }
};
}  // namespace types_internal

// A `unique_ptr` alternative that works with forward declared types.
//
// In order to make this work, the macro `MBO_TYPES_OPAQUE_HOOKS(T)` has to be used after
// the definition of the type. Further, all users of the OpaquePtr type must have access to it.
// NOTE: The macro defines the necessary ADL functions as inline functions.
template<typename T>
using OpaquePtr = std::unique_ptr<T, types_internal::OpaquePtrDeleter<T>>;

// Determine whether a type `T` is an `OpaquePtr`.
template<typename T>
concept IsOpaquePtr = requires {
  typename T::element_type;
  requires std::same_as<T, OpaquePtr<typename T::element_type>>;
};

// Similar to `std::make_unique` but for `OpaquePtr`.
//
// In order to make this work, the macro `MBO_TYPES_OPAQUE_HOOKS(T)` has to be used after
// the definition of the type. Further, all users of the OpaquePtr type must have access to it.
// NOTE: The macro defines the necessary ADL functions as inline functions.
template<typename T, typename... Args>
OpaquePtr<T> MakeOpaquePtr(Args&&... args) {
  ::mbo::types::OpaquePtr<T> ptr;
  OpaquePtrMakerHook(ptr, std::forward<Args>(args)...);
  return ptr;
}

// OpaqueValue defines a wrapper around a type `T` as an opaque std::unique_ptr that can be
// accessed, compared and hashed (absl and std) but will never be a `nullptr`.
//
// In order to make this work, the macro `MBO_TYPES_OPAQUE_HOOKS(T)` has to be used after
// the definition of the type. Further, all users of the OpaquePtr type must have access to it.
// NOTE: The macro defines the necessary ADL functions as inline functions.
template<typename T>
struct OpaqueValue {
  using element_type = T;  // NOLINT(readability-identifier-naming)

  ~OpaqueValue() = default;

  OpaqueValue()
  requires(std::is_default_constructible_v<T>)
      : ptr_(MakeOpaquePtr<T>()) {}

  template<typename... Args>
  explicit OpaqueValue(Args&&... args)
  requires ConstructibleFrom<T, Args...>
      : ptr_(MakeOpaquePtr<T>(std::forward<Args>(args)...)) {}

  OpaqueValue(OpaqueValue&&) noexcept = default;
  OpaqueValue& operator=(OpaqueValue&&) noexcept = default;

  OpaqueValue(const OpaqueValue& other)
  requires(std::is_copy_constructible_v<T>)
      : ptr_(MakeOpaquePtr<T>(*other.ptr_)) {}

  OpaqueValue& operator=(const OpaqueValue& other)
  requires(std::is_copy_constructible_v<T>) {
    if (&other != this) {
      ptr_ = MakeOpaquePtr<T>(*other.ptr_);
    }
    return *this;
  }

  template<typename... Args>
  explicit OpaqueValue(Args&&... args) : ptr_(MakeOpaquePtr<T>(std::forward<Args>(args)...)) {}

  T& operator*() { return *ptr_; }

  T* operator->() { return &*ptr_; }

  const T& operator*() const { return *ptr_; }

  const T* operator->() const { return &*ptr_; }

  auto operator<=>(const OpaqueValue& other) const
  requires std::three_way_comparable<T> {
    return *ptr_ <=> *other.ptr_;
  }

  auto operator<=>(const T& other) const
  requires std::three_way_comparable<T> {
    return *ptr_ <=> other;
  }

  template<typename H>
  friend H AbslHashValue(H hash, const OpaqueValue<T>& ptr) {
    return H::combine(std::move(hash), absl::HashOf<>(*ptr.ptr_));
  }

 private:
  OpaquePtr<T> ptr_;  // Will never be nullptr
};

// Determine whether a type `T` is an `OpaqueValue`.
template<typename T>
concept IsOpaqueValue = requires {
  typename T::element_type;
  requires std::same_as<T, OpaqueValue<typename T::element_type>>;
};

// OpaqueContainer wraps a container into an opaque std::unique_ptr and allows direct access for
// most of the STL container functions (in particular std::vector). That is, the majority of the
// containers functions are directly available in the type.
//
// In order to make this work, the macro `MBO_TYPES_OPAQUE_HOOKS(T)` has to be used after
// the definition of the type. Further, all users of the OpaquePtr type must have access to it.
// NOTE: The macro defines the necessary ADL functions as inline functions.
//
// NOTE: If the container is a `std` type, then the `MBO_TYPES_OPAQUE_HOOKS` must be placed in the
// namespace `std`.
//
// Example:
//
// ```
// namespace mine {
// using OpaqueStringVector = OpaqueContainer<std::vector<std::string>>;
// } // namespace mine
//
// namespace std {
// MBO_TYPES_OPAQUE_HOOKS(std::vector<std::string>);
// }  // namespace std
//
// Practically this type allows to change container fields to smart pointer wrapped container fields
// without the need to change any other code. Example:
//
// ```
// struct Mine {
//   std::vector<std::string> data;
// };
// ```
//
// Can be modified to:
//
// ```
// struct Mine {
//   mbo::types::OpaqueContainer<std::vector<std::string>> data;
// };
// ```
//
// In the above no other code needs to change (as long as the necessary function was already added).
// The type guarantees, just like in the original form, that the field `data` is always usable, or
// rather never has a `nullptr`. The advantage here is that now the container type or its value_type
// can be forward declared.
template<typename Container>
struct OpaqueContainer : ContainerProxy<OpaqueValue<Container>, Container> {};

}  // namespace mbo::types

// Macro to define an ADL deletion hook for the specified type to be used with `OpaquePtr`.
// NOLINTBEGIN(cppcoreguidelines-macro-usage,bugprone-macro-parentheses)
#define MBO_TYPES_OPAQUE_HOOKS(T)                                                   \
  template<typename... Args>                                                        \
  inline void OpaquePtrMakerHook(::mbo::types::OpaquePtr<T>& ptr, Args&&... args) { \
    ptr.reset(new T(std::forward<Args>(args)...));                                  \
  }                                                                                 \
  inline void OpaquePtrDeleterHook(T* ptr) {                                        \
    delete ptr; /* NOLINT(cppcoreguidelines-owning-memory) */                       \
  }

// NOLINTEND(cppcoreguidelines-macro-usage,bugprone-macro-parentheses)

namespace std {
// NOLINTBEGIN(cert-dcl58-cpp)
template<typename T>
requires(requires(const T& val) { std::hash<T>{}; })
struct hash<mbo::types::OpaqueValue<T>> {
  constexpr std::size_t operator()(const mbo::types::OpaqueValue<T>& ptr) const noexcept { return hash<T>{}(*ptr); }
};

// NOLINTEND(cert-dcl58-cpp)
}  // namespace std

#endif  // MBO_TYPES_OPAQUE_H_
