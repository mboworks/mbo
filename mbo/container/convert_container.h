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

#ifndef MBO_CONTAINER_CONVERT_CONTAINER_H_
#define MBO_CONTAINER_CONVERT_CONTAINER_H_

#include "mbo/types/traits.h"

namespace mbo::container {
namespace container_internal {

template<mbo::types::IsForwardIteratable Container, typename Func = mbo::types::types_internal::NoFunc>
class ConvertContainer final {
 private:
  using NoFunc = ::mbo::types::types_internal::NoFunc;

 public:
  using ValueType =
      mbo::types::types_internal::ValueOrResultT<typename std::remove_reference_t<Container>::value_type, Func>;

  explicit ConvertContainer(Container&& container) noexcept
  requires std::same_as<Func, NoFunc>
      : container_(std::forward<Container>(container)) {}

  explicit ConvertContainer(Container&& container, Func&& convert) noexcept
  requires(!std::same_as<Func, NoFunc>)
      : container_(std::forward<Container>(container)), convert_(std::forward<Func>(convert)) {}

  ~ConvertContainer() = default;
  ConvertContainer(const ConvertContainer&) = delete;
  ConvertContainer& operator=(const ConvertContainer&) = delete;
  ConvertContainer(ConvertContainer&&) noexcept = default;
  ConvertContainer& operator=(ConvertContainer&&) noexcept = default;

  template<typename ContainerOut>
  requires mbo::types::ContainerCopyConvertible<Container, ContainerOut, Func>
  operator ContainerOut() {  // NOLINT(*-explicit-constructor,hicpp-explicit-conversions)
    ContainerOut result;
    for (auto&& v : container_) {
      if constexpr (mbo::types::ContainerHasEmplace<ContainerOut, ValueType>) {
        if constexpr (std::same_as<Func, NoFunc>) {
          result.emplace(std::move(v));
        } else {
          result.emplace(convert_(std::move(v)));
        }
      } else if constexpr (mbo::types::ContainerHasEmplaceBack<ContainerOut, ValueType>) {
        if constexpr (std::same_as<Func, NoFunc>) {
          result.emplace_back(std::move(v));
        } else {
          result.emplace_back(convert_(std::move(v)));
        }
      } else if constexpr (mbo::types::ContainerHasInsert<ContainerOut, ValueType>) {
        if constexpr (std::same_as<Func, NoFunc>) {
          result.insert(std::move(v));
        } else {
          result.insert(convert_(std::move(v)));
        }
      } else if constexpr (mbo::types::ContainerHasPushBack<ContainerOut, ValueType>) {
        if constexpr (std::same_as<Func, NoFunc>) {
          result.push_back(std::move(v));
        } else {
          result.push_back(convert_(std::move(v)));
        }
      }
    }
    return result;
  }

 private:
  static inline constexpr NoFunc kNoFunc{};
  Container container_;
  Func convert_ = kNoFunc;
};

}  // namespace container_internal

// Copies `Container` into `ContainerOut` while converting values as needed.
// Requires that the values in `Container` can be emplaced or inserted into `ContainerOut`.
//
// Example:
//
//   std::vector<std::string_view> input{"foo", "bar", "baz"};
//   std::vector<string> strs = ConvertContainer(input);

template<mbo::types::IsForwardIteratable Container>
requires ::mbo::types::NotInitializerList<Container>
inline container_internal::ConvertContainer<Container> ConvertContainer(Container&& container) {
  return container_internal::ConvertContainer<Container>(std::forward<Container>(container));
}

template<mbo::types::IsForwardIteratable Container, typename Func>
requires ::mbo::types::NotInitializerList<Container>
inline container_internal::ConvertContainer<Container, Func> ConvertContainer(
    Container&& container,
    Func&& conversion) {
  return container_internal::ConvertContainer<Container, Func>(
      std::forward<Container>(container), std::forward<Func>(conversion));
}

template<typename U>
inline container_internal::ConvertContainer<const std::initializer_list<U>&> ConvertContainer(
    const std::initializer_list<U>& container) {
  return container_internal::ConvertContainer<const std::initializer_list<U>&>(container);
}

template<typename U, typename Func>
inline container_internal::ConvertContainer<const std::initializer_list<U>&, Func> ConvertContainer(
    const std::initializer_list<U>& container,
    Func&& conversion) {
  return container_internal::ConvertContainer<const std::initializer_list<U>&, Func>(
      container, std::forward<Func>(conversion));
}

}  // namespace mbo::container

#endif  // MBO_CONTAINER_CONVERT_CONTAINER_H_
