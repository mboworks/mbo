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

#ifndef MBO_TYPES_INTERNAL_EXTENDER_H_
#define MBO_TYPES_INTERNAL_EXTENDER_H_

// IWYU pragma: private, include "mbo/types/extend.h"

#include <array>
#include <concepts>  // IWYU pragma: keep
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "mbo/types/traits.h"  // IWYU pragma: keep
#include "mbo/types/tuple.h"   // IWYU pragma: keep
#include "mbo/types/tuple_extras.h"

namespace mbo::types {

// Base Extender implementation for CRTP functionality injection.
//
// This must be the base of every `Extend`ed struct's CRTP chain.
//
// This is only not in the internal namespace `types_internal` to shorten the
// resulting type names.
template<typename ActualType>
struct ExtendBase {
  using Type = ActualType;  // Used for type chaining in `UseExtender`

  // Construct from tuple.
  // The tuple elements must match the field types or the field types must allow conversion.
  template<typename... Args>
  requires IsConstructibleWithEmptyBaseAndArgs<Type, Args...>
  static Type ConstructFromTuple(std::tuple<Args...>&& args) {  // NOLINT(*-param-not-moved)
    return std::apply(ConstructFromArgs<Args...>, std::forward<std::tuple<Args...>>(args));
  }

  // Construct from separate arguments.
  // The arguments must match the field types or the field types must allow conversion.
  template<typename... Args>
  requires IsConstructibleWithEmptyBaseAndArgs<Type, Args...>
  static Type ConstructFromArgs(Args&&... args) {
    return Type{{}, std::forward<Args>(args)...};
  }

  // Construct from separate arguments that must be convertible (1:1 in order) to the field types.
  // Effectively calling `T(FieldType<N>(args<N>)...)`.
  template<typename... Args>
  requires TupleFieldsConstructible<
      std::tuple<Args...>,
      decltype(std::declval<const ExtendBase<ActualType>&>().ToTuple())>
  static Type ConstructFromConversions(Args&&... args) {
    return ConstructFromConversionsImpl(
        std::make_index_sequence<sizeof...(Args)>{}, std::make_tuple(std::forward<Args>(args)...));
  }

 protected:  // DO NOT expose anything publicly.
  auto ToTuple() const & { return StructToTuple(static_cast<const ActualType&>(*this)); }

  auto ToTuple() && { return StructToTuple(std::move(*this)); }

 private:  // DO NOT expose anything publicly.
  // The `index_sequence` is a pure tag - it carries `Idx...` in its type and has
  // no value to read, so it is deliberately unnamed. And `args` IS consumed: it
  // is forwarded element-wise below via `std::forward<...>(std::get<Idx>(args))`,
  // one forward per field, which the check does not recognise as moving `args`.
  template<std::size_t... Idx, typename... Args>
  // NOLINTNEXTLINE(hicpp-named-parameter,readability-named-parameter,cppcoreguidelines-rvalue-reference-param-not-moved)
  static Type ConstructFromConversionsImpl(std::index_sequence<Idx...>, std::tuple<Args...>&& args) {
    using FieldTypes = decltype(std::declval<const ExtendBase<ActualType>&>().ToTuple());
    using ArgTypes = std::tuple<Args...>;
    return ConstructFromArgs(
        std::remove_cvref_t<std::tuple_element_t<Idx, FieldTypes>>(
            std::forward<std::tuple_element_t<Idx, ArgTypes>>(std::get<Idx>(args)))...);
  }

  template<typename U, typename Extender>
  friend struct UseExtender;
  friend class Stringify;
};

namespace types_internal {

// Determine whether type `Extended` is an `Extend`ed type.
//
// This is an incomplete concept and the public version `mbo::types::IsExtended` should be used.
template<typename Extended>
concept IsExtended = requires {
  typename Extended::Type;
  typename Extended::RegisteredExtenders;
  typename Extended::UnexpandedExtenders;
  requires std::is_base_of_v<ExtendBase<typename Extended::Type>, Extended>;
  requires mbo::types::IsTuple<typename Extended::RegisteredExtenders>;
  requires mbo::types::IsTuple<typename Extended::UnexpandedExtenders>;
  {
    Extended::RegisteredExtenderNames()
  } -> std::same_as<std::array<std::string_view, std::tuple_size_v<typename Extended::RegisteredExtenders>>>;
};

// Access to the extender implementation for constructing the CRTP chain using
// `ExtendBuildChain`.
// This is done via a distinct struct, so that the non specialized templates
// can be private.
template<typename ExtenderBase, typename Extender>
struct UseExtender : Extender::template Impl<ExtenderBase> {};

}  // namespace types_internal
}  // namespace mbo::types

#endif  // MBO_TYPES_INTERNAL_EXTENDER_H_
