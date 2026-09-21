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

#ifndef MBO_TYPES_NO_DESTRUCT_H_
#define MBO_TYPES_NO_DESTRUCT_H_

#include <initializer_list>
#include <memory>  // IWYU pragma: keep
#include <type_traits>
#include <utility>

#include "absl/hash/hash.h"
#include "absl/strings/str_format.h"

namespace mbo::types {

// NOLINTBEGIN(*-pro-type-union-access)
// NoDestruct holds its value in a union precisely so the destructor is never
// run; accessing the member is the point of the type.

// NOLINTBEGIN(*-pro-type-member-init)

// `NoDestruct` supports static const and, when T can be constant-evaluated, static constexpr
// values. It uses constexpr `std::construct_at` because placement new is not permitted in constant
// evaluation. The C++23 compiler baseline provides that operation; whether a particular
// `NoDestruct<T>` is constexpr still depends on T and all of its fields.
template<typename T>
class NoDestruct final {
 private:
  struct None final {};

  union Data {
    constexpr Data() noexcept : none{} {}

    constexpr ~Data() noexcept {}

    Data(const Data&) = delete;
    Data& operator=(const Data&) = delete;
    Data(Data&&) = delete;
    Data& operator=(Data&&) = delete;

    None none;
    T value;
  };

 public:
  constexpr NoDestruct() noexcept(std::is_nothrow_default_constructible_v<T>) {
    std::construct_at(std::addressof(data_.value));
  }

  constexpr explicit NoDestruct(T arg) noexcept(std::is_nothrow_move_constructible_v<T>) {
    std::construct_at(std::addressof(data_.value), std::move(arg));
  }

  template<typename... Args>
  constexpr explicit NoDestruct(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    std::construct_at(std::addressof(data_.value), std::forward<Args>(args)...);
  }

  template<typename U>
  constexpr NoDestruct(const std::initializer_list<U>& args) noexcept(
      std::is_nothrow_constructible_v<T, const std::initializer_list<U>&>) {
    std::construct_at(std::addressof(data_.value), args);
  }

  constexpr ~NoDestruct() noexcept = default;

  NoDestruct(const NoDestruct&) = delete;
  NoDestruct(NoDestruct&&) = delete;
  NoDestruct& operator=(const NoDestruct&) = delete;
  NoDestruct& operator=(NoDestruct&&) = delete;

  constexpr const T& Get() const noexcept { return data_.value; }

  constexpr const T& operator*() const noexcept { return Get(); }

  constexpr const T* operator->() const noexcept { return &Get(); }

  constexpr T& Get() noexcept { return data_.value; }

  constexpr T& operator*() noexcept { return Get(); }

  constexpr T* operator->() noexcept { return &Get(); }

  template<typename H>
  friend H AbslHashValue(H hash, const NoDestruct<T>& v) {
    return H::combine(std::move(hash), v.Get());
  }

  template<typename Sink>
  friend void AbslStringify(Sink& sink, const NoDestruct<T>& v) {
    absl::Format(&sink, "%v", v.Get());
  }

 private:
  Data data_ = {};
};

// NOLINTEND(*-pro-type-member-init)

// NOLINTEND(*-pro-type-union-access)

}  // namespace mbo::types

#endif  // MBO_TYPES_NO_DESTRUCT_H_
