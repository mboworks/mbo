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

#ifndef MBO_TYPES_CONTAINER_PROXY_H_
#define MBO_TYPES_CONTAINER_PROXY_H_

#include <compare>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "absl/hash/hash.h"

namespace mbo::types {

// ContainerProxy is a wrapper for a type `T` which provides mutable and const access to a container.
//
// Example:
//
// ```
// // Data defines a struct with a field `data` that holds a container in a `std::unique_ptr`.
// // The type provides the two access methods `GetData`.
// template<typename Container = std::vector<std::string>>
// struct Data {
//  using ContainerType = Container;
//
//  Data() : data(std::make_unique<ContainerType>()) {}
//
//  std::unique_ptr<ContainerType> data;
//
//  ContainerType& GetData() { return *data; }
//
//  const ContainerType& GetData() const { return *data; }
// };
//
// template<typename C = std::vector<std::string>>
// using Proxy = ContainerProxy<Data<C>, C, &C::GetData, &C::GetData>;
// ```
//
// In other words `ContainerProxy` allows to add container access to a field within a struct.
// This includes smart pointers like `std::unique_ptr` and `std::shared_ptr` which work directly.
// That means that the `ContainerProxy` can also be used to add container access to smart pointers.
//
// For further application check `mbo::types::OpaqueContainer` which allows direct member field
// wrapping without the need to change any other code.
// ```
template<
    typename T,
    typename Container = T::element_type,
    Container& (T::*GetMutable)() = &T::operator*,
    const Container& (T::*GetConst)() const = &T::operator*>
requires requires { typename std::remove_cvref_t<Container>::value_type; }
struct ContainerProxy : T {
 private:
  using C = std::remove_cvref_t<Container>;
  using CC = const C;

  constexpr auto& Get() {
    T* self = static_cast<T*>(this);
    return (self->*GetMutable)();
  }

  constexpr const auto& Get() const {
    const T* self = static_cast<const T*>(this);
    return (self->*GetConst)();
  }

 public:
  using size_type = C::size_type;
  using value_type = C::value_type;

  // NOLINTBEGIN(readability-identifier-naming)
  // clang-format off
  constexpr auto begin() const requires(requires(const C& v) { v.begin(); }) { return Get().begin(); }
  constexpr auto end() const requires(requires(const C& v) { v.end(); }) { return Get().end(); }
  constexpr auto begin() requires(requires(C& v) { v.begin(); }) { return Get().begin(); }
  constexpr auto end() requires(requires(C& v) { v.end(); }) { return Get().end(); }

  constexpr auto rbegin() const requires(requires(const C& v) { v.rbegin(); }) { return Get().rbegin(); }
  constexpr auto rend() const requires(requires(const C& v) { v.rend(); }) { return Get().rend(); }
  constexpr auto rbegin() requires(requires(C& v) { v.rbegin(); }) { return Get().rbegin(); }
  constexpr auto rend() requires(requires(C& v) { v.rend(); }) { return Get().rend(); }

  constexpr auto cbegin() const requires(requires(const C& v) { v.cbegin(); }) { return Get().cbegin(); }
  constexpr auto cend() const requires(requires(const C& v) { v.cend(); }) { return Get().cend(); }
  constexpr auto crbegin() const requires(requires(const C& v) { v.crbegin(); }) { return Get().crbegin(); }
  constexpr auto crend() const requires(requires(const C& v) { v.crend(); }) { return Get().crend(); }

  constexpr bool empty() const requires(requires(const C& v) { v.empty(); }) { return Get().empty(); }
  constexpr auto size() const requires(requires(const C& v) { v.size(); }) { return Get().size(); }
  constexpr auto max_size() const requires(requires(const C& v) { v.max_size(); }) { return Get().max_size(); }
  constexpr auto capacity() const requires(requires(const C& v) { v.capacity(); }) { return Get().capacity(); }

  constexpr void clear() requires(requires(const C& v) { v.clear(); }) { Get().clear(); }
  constexpr void reserve(size_type capacity) requires(requires(C& v) { v.reserve(std::declval<size_type>()); }) { Get().reserve(capacity); }
  constexpr void resize(size_type size) requires(requires(C& v) { v.resize(std::declval<size_type>()); }) { Get().resize(size); }

  constexpr auto& at(std::size_t pos) const requires(requires(const C& v) { v.at(0); }) { return Get().at(pos); }
  constexpr auto& at(std::size_t pos) requires(requires(C& v) { v.at(0); }) { return Get().at(pos); }
  constexpr auto& operator[](std::size_t pos) const requires(requires(const C& v) { v[pos]; }) { return at(pos); }
  constexpr auto& operator[](std::size_t pos) requires(requires(C& v) { v[pos]; }) { return at(pos); }

  constexpr void push_back(const value_type& arg)
  requires(requires(C& v) { v.push_back(std::declval<const value_type&>()); }) {
    Get().push_back(arg);
  }

  constexpr void push_back(value_type&& arg)
  requires(requires(C& v) { v.push_back(std::declval<value_type>()); }) {
    Get().push_back(std::move(arg));
  }

  constexpr void push_front(const value_type& arg)
  requires(requires(C& v) { v.push_front(std::declval<const value_type&>()); }) {
    Get().push_front(arg);
  }

  constexpr void push_front(value_type&& arg)
  requires(requires(C& v) { v.push_front(std::declval<value_type>()); }) {
    Get().push_front(std::move(arg));
  }

  template<typename... Args>
  constexpr auto& emplace_back(Args&&... args)
  requires(requires(C& v) { v.emplace_back(std::declval<Args>()...); }) {
    return Get().emplace_back(std::forward<Args>(args)...);
  }

  template<typename... Args>
  constexpr auto& emplace_front(Args&&... args)
  requires(requires(C& v) { v.emplace_front(std::declval<Args>()...); }) {
    return Get().emplace_front(std::forward<Args>(args)...);
  }

  constexpr auto& back() const requires(requires(const C& v) { v.back(); }) { return Get().back(); }
  constexpr auto& back() requires(requires(C& v) { v.back(); }) { return Get().back(); }
  constexpr auto& front() const requires(requires(const T& v) { v.front(); }) { return Get().front(); }
  constexpr auto& front() requires(requires(C& v) { v.front(); }) { return Get().front(); }

  constexpr void pop_back() requires(requires(C& v) { v.pop_back(); }) { Get().pop_back(); }
  constexpr void pop_front() requires(requires(C& v) { v.pop_front(); }) { Get().pop_front(); }

  constexpr auto pop_back_value() noexcept(noexcept(Get().pop_back_value()))
  requires requires(C& container) { container.pop_back_value(); } {
    return Get().pop_back_value();
  }

  constexpr auto pop_front_value() noexcept(noexcept(Get().pop_front_value()))
  requires requires(C& container) { container.pop_front_value(); } {
    return Get().pop_front_value();
  }

  auto operator<=>(const ContainerProxy& other) const
  requires std::three_way_comparable<T>
  {
    return Get() <=> other.Get();
  }

  auto operator<=>(const C& other) const
  requires std::three_way_comparable<T>
  {
    return Get() <=> other;
  }

  template<typename H>
  friend H AbslHashValue(H hash, const ContainerProxy& proxy) {
    return H::combine(std::move(hash), absl::HashOf<>(proxy.Get()));
  }

  // clang-format on
  // NOLINTEND(readability-identifier-naming)
};

}  // namespace mbo::types

// Specialising `std::hash` for a program-defined type is explicitly permitted by
// [namespace.std]; what is UB is ADDING declarations to `std`, which this is not.
// The check cannot tell the two apart.
// NOLINTBEGIN(cert-dcl58-cpp)
namespace std {

template<typename T, typename R, std::remove_cvref_t<R>& (T::*F)(), const std::remove_cvref_t<R>& (T::*FC)() const>
requires(requires(const T& val) { std::hash<T>{}; })
struct hash<mbo::types::ContainerProxy<T, R, F, FC>> {
  constexpr std::size_t operator()(const mbo::types::ContainerProxy<T, R, F, FC>& ptr) const noexcept {
    return absl::HashOf<>(ptr);
  }
};
}  // namespace std

// NOLINTEND(cert-dcl58-cpp)

#endif  // MBO_TYPES_CONTAINER_PROXY_H_
