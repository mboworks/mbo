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

#ifndef MBO_TYPES_TRAITS_H_
#define MBO_TYPES_TRAITS_H_

#include <compare>
#include <concepts>  // IWYU pragma: keep
#include <cstddef>   // IWYU pragma: keep
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "mbo/types/internal/decompose_count.h"          // IWYU pragma: export
#include "mbo/types/internal/is_braces_constructible.h"  // IWYU pragma: export
#include "mbo/types/internal/traits.h"                   // IWYU pragma: export
#include "mbo/types/tuple.h"                             // IWYU pragma: export
#include "mbo/types/variant.h"                           // IWYU pragma: export

namespace mbo::types {

// Determines whether `std::is_aggrgate_v<T> == true`.
template<typename T>
concept IsAggregate = types_internal::IsAggregate<T>;

template<typename T>
concept IsEmptyType = types_internal::IsEmptyType<T>;

// Implements a variant of `std::constructible_from` that works around its limitations to deal with array args.
//
// Instead of typing on use of `new` this allows proper concept use:
// ```
// template<typename... Args, typename = decltype(::new(std::declval<void*>()) T(std::declval<Args>()...))>
// template<typename... Args, typename = std::enable_if_t<ConstructibleFrom<T, Args...>>>
// ```
//
// The implementation uses the actual `std::constructible_from` and if that fails tries the alternative way.
// The alternative first requires `std::destructible` and then tries construction via in-place `new`.
template<typename T, typename... Args>
concept ConstructibleFrom =                 //
    std::constructible_from<T, Args...> ||  //
    (std::destructible<T> && requires(T value, Args... args) {
      { ::new (&value) T(args...) } -> std::same_as<T*>;
    });

// Verify whether `From` can used used to construct a `Into`.
// That allows the concept to be used for in-place/auto template parameters.
// See Compiler Explorer debug: https://godbolt.org/z/5vMEeGMMP
//
// Motivating example:
//
// A function that is meant to handle any string-like type (std::string/_view,
// char*, ...). That still supports perfect forwarding (or move). In order to
// have a short notation we need the `std::constructible_from` parameter order
// switched. That is what this concept does.
//
// ```
// struct Type {
//   Type(ConstructibleInto<std::string> auto&& v) : name_(std::forward<decltype(v)>(v)) {}
//
//   const std::string name;
// };
// ```
//
// Second variant, doing the same:
//
// ```
// struct Type {
//   template<ConstructibleInto<std::string> Str>
//   Type(Str&& v) : name_(int, std::forward<decltype(v)>(v)) {}
//
//   const std::string name;
// };
// ```
template<typename From, typename Into>
concept ConstructibleInto = ConstructibleFrom<Into, From>;

// If `T` is a struct and the below conditions are met, return the number of
// variable fields it decomposes into.
//
// Requirements:
// * T is an aggregate.
// * T has no or only empty base classes.
// * Has at least one field.
//
// GCC cannot always distinguish aggregate-initializer count from structured-
// binding arity, most notably for aggregates containing built-in arrays. Such
// a type can provide the exact structured-binding arity as a hidden friend:
//
//   friend consteval std::size_t MboTypesDecomposeCount(const T*) { return 2; }
//
// The returned count must exactly match the number of names required by a
// structured binding of T.
//
// This templated constant does not answer whether a tuple can be constructed,
// because en empty tuple can be created for an empty aggregate, but the latter
// cannot be decomposed with structured bindings.
//
// The empty aggregate to empty tuple can be checked with `CanCreateTuple`.
template<typename T>
inline constexpr std::size_t DecomposeCountV = types_internal::DecomposeCountImpl<T>::value;

// If a `T` cannot be decomposed, then `decompose_count` returns this value.
inline constexpr std::size_t NotDecomposableV = types_internal::NotDecomposableImpl::value;

// Returns whether `T` can be decomposed.
// This is only true if a non empty structured binding of T is possible:
//   const auto& [a0....] = T()
template<typename T>
concept IsDecomposable =
    types_internal::IsAggregate<T> && (DecomposeCountV<T> != 0) && (DecomposeCountV<T> != NotDecomposableV);

// Returns whether `T` can be constructed from `Args...`.
template<typename T, typename... Args>
inline constexpr bool IsBracesConstructibleV = types_internal::IsBracesConstructibleImplT<T, Args...>::value;

namespace types_internal {
template<typename Container>
concept IsForwardIteratableRaw = requires(Container container, const Container const_container) {
  requires std::forward_iterator<typename Container::iterator>;
  requires std::forward_iterator<typename Container::const_iterator>;
  requires std::same_as<typename Container::reference, typename Container::value_type&>
               || std::same_as<typename Container::reference, const typename Container::value_type&>;
  requires std::same_as<typename Container::const_reference, const typename Container::value_type&>;
  requires std::forward_iterator<typename Container::iterator>;
  requires std::forward_iterator<typename Container::const_iterator>;
  { container.begin() } -> std::same_as<typename Container::iterator>;
  { container.end() } -> std::same_as<typename Container::iterator>;
  { const_container.begin() } -> std::same_as<typename Container::const_iterator>;
  { const_container.end() } -> std::same_as<typename Container::const_iterator>;
};
}  // namespace types_internal

// Identifies `Container` types that are at least forward iterable (this include `std::initializer_list`).
template<typename Container>
concept IsForwardIteratable = types_internal::IsForwardIteratableRaw<std::remove_cvref_t<Container>>;

namespace types_internal {

template<typename Container>
concept ContainerIsForwardIteratableRaw = requires(Container container, const Container const_container) {
  requires IsForwardIteratableRaw<Container>;
  requires std::signed_integral<typename Container::difference_type>;
  requires std::same_as<
      typename Container::difference_type,
      typename std::iterator_traits<typename Container::iterator>::difference_type>;
  requires std::same_as<
      typename Container::difference_type,
      typename std::iterator_traits<typename Container::const_iterator>::difference_type>;
  // Technically `cbegin`, `cend`, `empty` and `size` are not required.
  // But we require them nonetheless (for now) as decent container implementation will provide them.
  { container.cbegin() } -> std::same_as<typename Container::const_iterator>;
  { container.cend() } -> std::same_as<typename Container::const_iterator>;
  { container.empty() } -> std::same_as<bool>;
  { container.size() } -> std::same_as<typename Container::size_type>;
};

template<typename Container>
concept ContainerHasInputIteratorImpl = requires(Container container, const Container const_container) {
  typename Container::iterator;
  typename Container::const_iterator;
  requires std::input_iterator<typename Container::iterator>;
  requires std::input_iterator<typename Container::const_iterator>;
  { container.begin() } -> std::same_as<typename Container::iterator>;
  { container.end() } -> std::same_as<typename Container::iterator>;
  { const_container.begin() } -> std::same_as<typename Container::const_iterator>;
  { const_container.end() } -> std::same_as<typename Container::const_iterator>;
};

template<typename Container>
concept ContainerHasForwardIteratorImpl = requires(Container container, const Container const_container) {
  typename Container::iterator;
  typename Container::const_iterator;
  requires std::forward_iterator<typename Container::iterator>;
  requires std::forward_iterator<typename Container::const_iterator>;
  { container.begin() } -> std::same_as<typename Container::iterator>;
  { container.end() } -> std::same_as<typename Container::iterator>;
  { const_container.begin() } -> std::same_as<typename Container::const_iterator>;
  { const_container.end() } -> std::same_as<typename Container::const_iterator>;
};
}  // namespace types_internal

// Identifies STL like `Container` types that are at least iterable.
template<typename Container>
concept ContainerIsForwardIteratable = types_internal::ContainerIsForwardIteratableRaw<std::remove_cvref_t<Container>>;

// Similar to `ContainerIsForwardIteratable` but only checks for `begin`, `end` and iterators.
// Here the iterators must comply with `std::input_iterator`.
template<typename Container>
concept ContainerHasInputIterator = types_internal::ContainerHasInputIteratorImpl<std::remove_cvref_t<Container>>;

// Similar to `ContainerIsForwardIteratable` but only checks for `begin`, `end` and iterators.
// Here the iterators must comply with `std::forward_iterator`.
template<typename Container>
concept ContainerHasForwardIterator = types_internal::ContainerHasForwardIteratorImpl<std::remove_cvref_t<Container>>;

template<typename T>
concept HasDifferenceType = requires { typename T::difference_type; };

namespace types_internal {

template<typename T, bool = HasDifferenceType<T>>
struct GetDifferenceTypeImpl {
  using type = std::ptrdiff_t;
};

template<typename T>
struct GetDifferenceTypeImpl<T, true> {
  using type = T::difference_type;
};

}  // namespace types_internal

template<typename T>
using GetDifferenceType = types_internal::GetDifferenceTypeImpl<T>::type;

// Identifies std like `Container` types that support `emplace` with `ValueType`.
template<typename Container, typename ValueType>
concept ContainerHasEmplace = requires(Container container, ValueType new_value) {
  requires ContainerIsForwardIteratable<Container>;
  requires ConstructibleFrom<typename Container::value_type, ValueType>;
  { container.emplace(new_value) };  // Only tests whether emplace(<arg>) exists, not whether new_value can be that arg.
};

// Identifies std like `Container` types that support `emplace_back` with `ValueType`.
template<typename Container, typename ValueType>
concept ContainerHasEmplaceBack = requires(Container container, ValueType new_value) {
  requires ContainerIsForwardIteratable<Container>;
  requires ConstructibleFrom<typename Container::value_type, ValueType>;
  { container.emplace_back(new_value) };
};

// Identifies std like `Container` types that support `emplace` with `ValueType`.
template<typename Container, typename ValueType>
concept ContainerHasInsert = requires(Container container, ValueType new_value) {
  requires ContainerIsForwardIteratable<Container>;
  requires std::convertible_to<ValueType, typename Container::value_type>;
  { container.insert(new_value) };
};

// Identifies std like `Container` types that support `emplace_back` with `ValueType`.
template<typename Container, typename ValueType>
concept ContainerHasPushBack = requires(Container container, ValueType new_value) {
  requires ContainerIsForwardIteratable<Container>;
  requires std::convertible_to<ValueType, typename Container::value_type>;
  { container.push_back(new_value) };
};

namespace types_internal {

template<typename T>
struct IsInitializerListImpl : std::false_type {};

template<typename T>
struct IsInitializerListImpl<std::initializer_list<T>> : std::true_type {};

}  // namespace types_internal

template<typename T>
concept IsInitializerList = types_internal::IsInitializerListImpl<std::remove_cvref_t<T>>::value;

template<typename T>
concept NotInitializerList = !IsInitializerList<T>;

template<typename T>
concept HasValueType = requires { typename T::value_type; };

namespace types_internal {

template<std::forward_iterator It, bool = HasValueType<It>>
struct ForwardIteratorValueTypeImpl {
  using type = It::value_type;
};

template<std::forward_iterator It>
struct ForwardIteratorValueTypeImpl<It, false> {
  using type = std::remove_reference_t<decltype(*std::declval<It&>())>;
};

}  // namespace types_internal

template<std::forward_iterator It>
using ForwardIteratorValueType = types_internal::ForwardIteratorValueTypeImpl<It>::type;

template<ContainerHasInputIterator Container>
using ContainerConstIteratorValueType =
    std::iterator_traits<typename std::remove_cvref_t<Container>::const_iterator>::value_type;

namespace types_internal {

template<typename T>
struct IsCharArrayImpl : std::false_type {};

template<>
struct IsCharArrayImpl<char*> : std::true_type {};

template<>
struct IsCharArrayImpl<const char*> : std::true_type {};

}  // namespace types_internal

template<typename T>
concept IsCharArray = types_internal::IsCharArrayImpl<std::decay_t<T>>::value;

template<typename T>
concept NotIsCharArray = !IsCharArray<T>;

namespace types_internal {

struct NoFunc final {};

template<typename T, typename Func>
struct ValueOrResult {
  using type = std::invoke_result_t<Func, T>;
};

template<typename T>
struct ValueOrResult<T, NoFunc> {
  using type = T;
};

template<typename T, typename Func>
using ValueOrResultT = ValueOrResult<T, Func>::type;

template<typename ContainerIn, typename ContainerOut, typename Func = NoFunc>
concept ContainerCopyConvertibleRaw =                                              //
    (ContainerIsForwardIteratable<ContainerIn> || IsInitializerList<ContainerIn>)  //
    &&ContainerIsForwardIteratable<ContainerOut>
    && (ContainerHasEmplace<ContainerOut, ValueOrResultT<typename ContainerIn::value_type, Func>>
        || ContainerHasEmplaceBack<ContainerOut, ValueOrResultT<typename ContainerIn::value_type, Func>>
        || ContainerHasInsert<ContainerOut, ValueOrResultT<typename ContainerIn::value_type, Func>>
        || ContainerHasPushBack<ContainerOut, ValueOrResultT<typename ContainerIn::value_type, Func>>);

}  // namespace types_internal

template<typename ContainerIn, typename ContainerOut, typename Func = types_internal::NoFunc>
concept ContainerCopyConvertible = types_internal::
    ContainerCopyConvertibleRaw<std::remove_cvref_t<ContainerIn>, std::remove_reference_t<ContainerOut>, Func>;

template<typename T>
concept IsPair = requires(T pair) {
  typename T::first_type;
  typename T::second_type;
  requires std::same_as<T, std::pair<typename T::first_type, typename T::second_type>>;
};

template<typename T>
concept IsPairFirstStr = IsPair<T> && requires(T pair) {
  typename T::first_type;
  requires std::is_convertible_v<typename T::first_type, std::string_view>;
};

template<typename T>
concept IsSet = requires {
  typename T::value_type;
  typename T::key_type;
  typename T::allocator_type;
  requires std::same_as<T, std::set<typename T::value_type, typename T::key_type, typename T::allocator_type>>;
};

template<typename T>
concept IsVector = requires {
  typename T::value_type;
  typename T::allocator_type;
  requires std::same_as<T, std::vector<typename T::value_type, typename T::allocator_type>>;
};

// Test whether `std::same_as` is true for one of a list of types `Ts`.
//
// Example: The following example takes an `int` or ` string`.
//
// ```
// template <typename T>
// requires(IsSameAsAnyOf<T, int, std::string>)
// void Func(T) {};
// ```
template<typename SameAs, typename... Ts>
concept IsSameAsAnyOf = (std::same_as<SameAs, Ts> || ...);

// Inversion of `IsSameAsAnyOf`.
template<typename SameAs, typename... Ts>
concept NotSameAsAnyOf = !IsSameAsAnyOf<SameAs, Ts...>;

// Test whether `std::same_as` is true for one of a list of types `Ts` after removing `const`,
// `volatile` or `&`.
//
// Example: The following example takes any form of `int`
// or `unsigned` where `const`, `volatile` or `&` are removed. So an `int*` would not be acceptable.
//
// ```
// template <typename T>
// requires(IsSameAsAnyOfRaw<T, int, unsigned>)
// void Func(T) {};
// ```
template<typename SameAs, typename... Ts>
concept IsSameAsAnyOfRaw = IsSameAsAnyOf<std::remove_cvref_t<SameAs>, std::remove_cvref_t<Ts>...>;

// Inverse of the above. So this prevents specific types. This is necessary when building overload
// alternatives.
template<typename SameAs, typename... Ts>
concept NotSameAsAnyOfRaw = !IsSameAsAnyOfRaw<SameAs, Ts...>;

// Test whether two types are identical after removing `const`, `volatile` or `&`.
template<typename SameAs, typename T>
concept IsSameAsRaw = std::same_as<std::remove_cvref_t<SameAs>, std::remove_cvref_t<T>>;

// Determines whether `T` can be constructed from an empty base and `Args`.
//
// This is used in mbo::types::Extend<...>::ConstructFrom{Tuple|Args}.
//
// See: https://godbolt.org/z/3nnsG6bEb
template<typename T, typename... Args>
concept IsConstructibleWithEmptyBaseAndArgs = requires(Args&&... args) {
  { T{{}, {std::forward<Args>(args)}...} };
};

template<typename T>
struct IsSmartPtrImpl : std::false_type {};

template<typename T>
struct IsSmartPtrImpl<std::shared_ptr<T>> : std::true_type {};

template<typename T>
struct IsSmartPtrImpl<std::unique_ptr<T>> : std::true_type {};

template<typename T>
struct IsSmartPtrImpl<std::weak_ptr<T>> : std::true_type {};

// Identify smart pointers (`std::unique_ptr`, `std::shared_ptr`, `std::weak_ptr`).
// Can be exptended by providing additional specicializations for `IsSmartPtrImpl`.
template<typename T>
concept IsSmartPtr = IsSmartPtrImpl<T>::value;

template<typename T>
concept IsOptional = requires {
  typename T::value_type;
  requires std::same_as<std::optional<typename T::value_type>, T>;
};

// Determines whether a type is a `std::reference_wrapper`.
template<typename T>
concept IsReferenceWrapper = requires {
  typename T::type;
  requires std::same_as<std::reference_wrapper<typename T::type>, T>;
};
// Test whether a type is a container whose elements are pairs and whose keys
// are convertible to a std::string_view.
template<typename T>
concept IsStringKeyedContainer =
    ContainerIsForwardIteratable<T> && IsPairFirstStr<std::remove_cvref_t<typename T::value_type>>;

// Type           IsScalar     IsArithmetic IsIntegral   IsFloatingPoint
// bool           true         true         true         false
// char           true         true         true         false
// char const     true         true         true         false
// int            true         true         true         false
// int const      true         true         true         false
// float          true         true         false        true
// double         true         true         false        true
// long double    true         true         false        true
// std::size_t    true         true         true         false
// int*           true         false        false        false
// float*         true         false        false        false
// int&           false        false        false        false
// float&         false        false        false        false
// See: https://godbolt.org/z/4sr89s9Y5
template<typename T>
concept IsArithmetic = std::is_arithmetic_v<T>;

template<typename T>
concept IsIntegral = std::integral<T>;

template<typename T>
concept IsScalar = std::is_scalar_v<T>;

template<typename T>
concept IsFloatingPoint = std::floating_point<T>;

namespace types_internal {
template<typename T, typename Cat>
concept ComparesAs = std::same_as<std::common_comparison_category_t<T, Cat>, Cat>;

}  // namespace types_internal

// Similar to `std::three_way_comparable_with` but we only verify that `L <=> R` can be interpreted as `Cat` in the
// presented argument order.
//
// This means there is no need for a common reference - which usually effectively means one side needs to be converted.
// There is further no requirement for `L <=> L`, `R <=> R` or ordering requirements for `L` or `R`.
//
// It also means that there is no guaranteed that `R <=> L` is valid.
template<typename Lhs, typename Rhs, typename Cat = std::partial_ordering>
concept ThreeWayComparableTo =
    requires(const std::remove_reference_t<Lhs>& lhs, const std::remove_reference_t<Rhs>& rhs) {
      { lhs <=> rhs } -> types_internal::ComparesAs<Cat>;
    };
}  // namespace mbo::types

#endif  // MBO_TYPES_TRAITS_H_
