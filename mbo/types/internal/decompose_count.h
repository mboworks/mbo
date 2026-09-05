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

#ifndef MBO_TYPES_INTERNAL_DECOMPOSE_COUNT_H_
#define MBO_TYPES_INTERNAL_DECOMPOSE_COUNT_H_

// Max fields: 40

// IWYU pragma: private, include "mbo/types/traits.h"

#include <limits>
#include <memory>
#include <string>       // IWYU pragma: keep
#include <string_view>  // IWYU pragma: keep
#include <tuple>
#include <type_traits>
#include <utility>  // IWYU pragma: keep

#include "mbo/types/internal/cases.h"  // IWYU pragma: keep
#include "mbo/types/internal/is_braces_constructible.h"
#include "mbo/types/internal/traits.h"  // IWYU pragma: keep
#include "mbo/types/template_search.h"  // IWYU pragma: keep

namespace mbo::types::types_internal {

// NOLINTBEGIN(*-magic-numbers)

struct NotDecomposableImpl : std::integral_constant<std::size_t, std::numeric_limits<std::size_t>::max()> {};

constexpr std::size_t kNotDecomposableValue = NotDecomposableImpl::value;

inline constexpr std::size_t kMaxSupportedFieldCount = 50;

template<typename T, std::size_t MaxArgs, typename... Args>
constexpr std::size_t StructCtorArgCountMaxFunc() {
  if constexpr (sizeof...(Args) > MaxArgs) {  // NOLINT(bugprone-branch-clone)
    return 0;
  } else {
    constexpr std::size_t kCount = StructCtorArgCountMaxFunc<T, MaxArgs, Args..., AnyType>();
    if constexpr (kCount) {
      return kCount;
    } else if constexpr (IsBracesConstructibleImplT<T, Args...>::value) {
      return sizeof...(Args);
    } else {
      return 0;
    }
  }
}

template<typename T>
struct StructCtorArgCountMaxImpl
    : std::integral_constant<  //
          std::size_t,
          IsAggregate<T> ? StructCtorArgCountMaxFunc<T, kMaxSupportedFieldCount>() : 0> {};

template<typename T>
struct StructCtorArgCountMaxT : StructCtorArgCountMaxImpl<std::remove_cvref_t<T>> {};

template<typename T>
inline constexpr std::size_t StructCtorArgCountMaxV = StructCtorArgCountMaxT<T>::value;

template<typename T, bool kRequireNonEmpty>
struct AggregateHasBaseRaw
    : std::bool_constant<
          IsAggregate<T> && !IsEmptyType<T>
              ? StructCtorArgCountMaxFunc<T, kMaxSupportedFieldCount, AnyTypeIf<T, true, kRequireNonEmpty>>() != 0
              : false> {};

template<typename T>
struct AggregateHasNonEmptyBaseRaw : AggregateHasBaseRaw<T, true> {};

template<typename T>
struct AggregateHasNonEmptyBaseImpl : AggregateHasNonEmptyBaseRaw<std::remove_cvref_t<T>> {};

// Crude approximation of whether a `T` has a non empty base struct/class.
//
// NOTE: The behavior is undefined when using this on non aggregate types.
template<typename T>
concept AggregateHasNonEmptyBase = AggregateHasNonEmptyBaseImpl<T>::value;

// OverloadSets (see below) are the preferred solution but it requires Clang and can confuse ClangD.
#if defined(__clang__) && !defined(IS_CLANGD)

constexpr bool kMboTypesDecomposeCountUseOverloadSet = true;

# define MBO_TYPES_DECOMPOSE_COUNT_USE_OVERLOAD_SET 1

// ----------------------------------------------------
// This version implements the somewhat known Overloadset solution with additional identification of
// non admissible types. This works well for Clang compilers but fails to compile (even) in GCC 14.
// For the GCC issue refer to: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=92402
// For the basic mechanism see: https://godbolt.org/z/Ejada5YEE

template<typename... Ts>
struct OverloadSet : Ts... {
  explicit OverloadSet(Ts&&... v) : Ts(std::move(v))... {}

  using Ts::operator()...;
  // Unfortunately we cannot use an alternative fallback using `...` arg here:
  // auto operator()(...) const { return std::integral_constant<std::size_t, 0>{}; };
  // but in the below we can use further `constexpr if ...`.
};

template<typename... Ts>
auto MakeOverloadedSet(Ts&&... v) {
  return OverloadSet<Ts...>(std::forward<Ts>(v)...);
}

// __extension__ on each statement expression: the GNU extension is this file's
// deliberate mechanism, and the keyword scopes -Wpedantic acceptance to exactly
// these uses - under EVERY compiler, unlike a -Wno- flag tied to one config.
template<typename T>
auto DecomposeCountFunc(T&& v) {
  auto fn1 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1] = std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 1>{};
  })) { return {}; };
  auto fn2 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2] = std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 2>{};
  })) { return {}; };
  auto fn3 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3] = std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 3>{};
  })) { return {}; };
  auto fn4 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4] = std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 4>{};
  })) { return {}; };
  auto fn5 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5] = std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 5>{};
  })) { return {}; };
  auto fn6 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6] = std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 6>{};
  })) { return {}; };
  auto fn7 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7] = std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 7>{};
  })) { return {}; };
  auto fn8 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8] = std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 8>{};
  })) { return {}; };
  auto fn9 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9] = std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 9>{};
  })) { return {}; };
  auto fn10 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10] = std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 10>{};
  })) { return {}; };
  auto fn11 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11] = std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 11>{};
  })) { return {}; };
  auto fn12 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12] = std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 12>{};
  })) { return {}; };
  auto fn13 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13] = std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 13>{};
  })) { return {}; };
  auto fn14 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 14>{};
  })) { return {}; };
  auto fn15 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 15>{};
  })) { return {}; };
  auto fn16 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 16>{};
  })) { return {}; };
  auto fn17 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 17>{};
  })) { return {}; };
  auto fn18 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 18>{};
  })) { return {}; };
  auto fn19 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 19>{};
  })) { return {}; };
  auto fn20 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 20>{};
  })) { return {}; };
  auto fn21 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 21>{};
  })) { return {}; };
  auto fn22 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 22>{};
  })) { return {}; };
  auto fn23 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 23>{};
  })) { return {}; };
  auto fn24 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 24>{};
  })) { return {}; };
  auto fn25 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 25>{};
  })) { return {}; };
  auto fn26 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 26>{};
  })) { return {}; };
  auto fn27 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 27>{};
  })) { return {}; };
  auto fn28 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 28>{};
  })) { return {}; };
  auto fn29 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 29>{};
  })) { return {}; };
  auto fn30 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 30>{};
  })) { return {}; };
  auto fn31 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 31>{};
  })) { return {}; };
  auto fn32 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 32>{};
  })) { return {}; };
  auto fn33 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 33>{};
  })) { return {}; };
  auto fn34 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 34>{};
  })) { return {}; };
  auto fn35 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 35>{};
  })) { return {}; };
  auto fn36 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 36>{};
  })) { return {}; };
  auto fn37 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 37>{};
  })) { return {}; };
  auto fn38 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 38>{};
  })) { return {}; };
  auto fn39 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 39>{};
  })) { return {}; };
  auto fn40 = [](auto&& v) -> decltype(__extension__({
    [[maybe_unused]] auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40] =
        std::forward<decltype(v)>(v);
    std::integral_constant<std::size_t, 40>{};
  })) { return {}; };
  auto overload_set = MakeOverloadedSet(
      std::move(fn1), std::move(fn2), std::move(fn3), std::move(fn4), std::move(fn5), std::move(fn6), std::move(fn7),
      std::move(fn8), std::move(fn9), std::move(fn10), std::move(fn11), std::move(fn12), std::move(fn13),
      std::move(fn14), std::move(fn15), std::move(fn16), std::move(fn17), std::move(fn18), std::move(fn19),
      std::move(fn20), std::move(fn21), std::move(fn22), std::move(fn23), std::move(fn24), std::move(fn25),
      std::move(fn26), std::move(fn27), std::move(fn28), std::move(fn29), std::move(fn30), std::move(fn31),
      std::move(fn32), std::move(fn33), std::move(fn34), std::move(fn35), std::move(fn36), std::move(fn37),
      std::move(fn38), std::move(fn39), std::move(fn40));
  if constexpr (requires {
                  { overload_set(std::declval<T>()) };
                }) {
    return overload_set(std::forward<T>(v));
  } else {
    // Note that we could enable `IsAggregate<T> && IsEmptyType<T>` to return 0 for
    // empty, but we are not returning field counts here. We are retuning decompose counts here.
    return std::integral_constant<std::size_t, kNotDecomposableValue>{};
  }
}

template<typename T>
struct DecomposeCountImpl : decltype(DecomposeCountFunc(std::declval<T>())) /**/ {};

template<>
struct DecomposeCountImpl<void> : std::integral_constant<std::size_t, kNotDecomposableValue> {};

template<typename T>
concept DecomposeCondition = (DecomposeCountImpl<T>::value != kNotDecomposableValue);

// Put all into once struct.
template<typename T>
struct DecomposeInfo final {
  using Type = std::remove_cvref_t<T>;
  static constexpr bool kIsAggregate = IsAggregate<Type>;
  static constexpr bool kIsEmpty = IsEmptyType<Type>;
  static constexpr std::size_t kFieldCount = IsEmptyType<Type> ? 0 : DecomposeCountImpl<Type>::value;
  static constexpr std::size_t kDecomposeCount = DecomposeCountImpl<Type>::value;
  static constexpr bool kDecomposable = (kIsAggregate && !kIsEmpty) && (kDecomposeCount != kNotDecomposableValue);

  static std::string Debug(std::string_view separator = ", ") {
    std::string str;
    std::string_view sep;
# define DEBUG_ADD(f)                                                                             \
   str += std::string(sep) + #f + ": "                                                            \
          + (std::same_as<std::remove_cvref_t<decltype(DecomposeInfo<T>::f)>, bool>               \
                 ? std::string(DecomposeInfo<T>::f ? "Yes" : "No")                                \
                 : (std::same_as<std::remove_cvref_t<decltype(DecomposeInfo<T>::f)>, std::size_t> \
                            && DecomposeInfo<T>::f == NotDecomposableImpl::value                  \
                        ? std::string("N/A")                                                      \
                        : std::to_string(DecomposeInfo<T>::f)));                                  \
   sep = separator
    DEBUG_ADD(kIsAggregate);
    DEBUG_ADD(kIsEmpty);
    DEBUG_ADD(kFieldCount);
    DEBUG_ADD(kDecomposable);
    DEBUG_ADD(kDecomposeCount);
# undef DEBUG_ADD
    return str;
  }
};

#else  // defined(__clang__)
constexpr bool kMboTypesDecomposeCountUseOverloadSet = false;
// ----------------------------------------------------

// From
// https://github.com/mboworks/mbo/commit/03789fed711e9603170dda767b1ebe50be6df282
//
// Unlike Clang 16, GCC does not understand `decltype` of a lmbda performing
// structured-bindings, e.g.: `decltype([]() { const auto& [a0] = T(); }`.
// GCC further has a bug with primary lambda expressions in a decltype:
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=92402
//
// Thus GCC needs to count initializers and fields and base classes.
// The core idea of identifying the actual number of values a struct
// decomposes into for a structured-binding is extremely well discussed in
// https://towardsdev.com/counting-the-number-of-fields-in-an-aggregate-in-c-20-part-2-d3103dec734f
//
// However, that solution still does not respect base classes, which is why
// I originally gave up on that approach independently and developed the
// Clang solution instead. Finding this documentation was an excellent
// refresher on all the basic field handling. With that, I went on to
// identifying base classes and all conditions that structured-bindings
// apply while upgrading the core field/initializer handling.
//
// Some experiments were made here: https://godbolt.org/z/se7PcjqGz

// First determine how many initializer values can be used to initialize `T`.

template<typename T, typename... Args>
concept IsAggregateInitializable = IsAggregate<T> && requires { T{std::declval<Args>()...}; };

template<IsAggregate T, typename Indices>
struct IsAggregateInitializableFromIndices;

// Attempt initialization: Simply try any type for all Indices.
//
// The alternative is to already consider a number of base types in the front and then all non base
// types (aka the fields). This has the disadvantage that it effectively prevents `BinarySearch`
// from working in all cases. Plus, it means that for every test we have to try a given number of
// base types which multiplies the attempts O(N^2). Trying any type has a chance to work with
// `BinarySearch` plus `ReverseSearch`: O(log(N)) + O(N) --> O(N).
template<IsAggregate T, std::size_t... Indices>
struct IsAggregateInitializableFromIndices<T, std::index_sequence<Indices...>>
    : std::bool_constant<IsAggregateInitializable<T, AnyTypeN<Indices>...>> {};

template<typename T, std::size_t N>
concept IsAggregateInitializableWithNumArgs =
    IsAggregate<T> && IsAggregateInitializableFromIndices<T, std::make_index_sequence<N>>::value;

// Now determine how many fields that translates to.
// For that we try to replace initializers with braced initializers.

template<IsAggregate T>
struct AggregateInitializeTest {
  template<std::size_t N>
  struct IsInitializable : std::bool_constant<IsAggregateInitializableWithNumArgs<T, N>> {};
};

// Upper bound of how many initializers there actually may be.
// On a single byte there could be 8 bit fields, so we need to multiply with 8.
constexpr std::size_t kFieldBlowupFactor = 8;

template<typename T>
constexpr std::size_t kMaxPossibleFields = kFieldBlowupFactor * sizeof(T);

// Determine the highest number of possible initializers.

template<IsAggregate T>
struct AggregateInitializerMidCount
    : BinarySearch<AggregateInitializeTest<T>::template IsInitializable, 0, kMaxPossibleFields<T> + 1, 0> {};

// The preference is to find somewhat high value as fast as possible.
// Applying BinarySearch may however not work, as it may simply not try a workable number.
// In the failure case, the result is 0 and we will simply revert to worst case scenario for the next step.
// From the number we found so far, we try the highest number, in reverse order (starting at End - 1).
template<IsAggregate T>
struct AggregateInitializerCount
    : ReverseSearch<
          AggregateInitializeTest<T>::template IsInitializable,
          AggregateInitializerMidCount<T>::value,
          kMaxPossibleFields<T> + 1,
          0> {};

template<IsAggregate T, typename Indices, typename FieldIndices>
struct AggregateInitializeableWith;

// clang-format off
template<IsAggregate T, std::size_t... kIndices, std::size_t... kFieldIndices>
    struct AggregateInitializeableWith<T, std::index_sequence<kIndices...>, std::index_sequence<kFieldIndices...>>
    : std::bool_constant < requires {
  T{std::declval<AnyTypeN<kIndices>>()..., {std::declval<AnyNonBaseTypeN<kFieldIndices, T>>()...}};
} > {};

// clang-format on

template<typename T, std::size_t N, std::size_t M>
concept AggregateFieldAtInitializeableWithImpl =
    IsAggregate<T> && AggregateInitializeableWith<T, std::make_index_sequence<N>, std::make_index_sequence<M>>::value;

template<IsAggregate T, std::size_t N>
struct AggregateFieldInitTest {
  template<std::size_t M>
  struct IsInitializable : std::bool_constant<AggregateFieldAtInitializeableWithImpl<T, N, M>> {};
};

template<IsAggregate T, std::size_t N>
struct AggregateFieldInitializerCount
    : BinarySearch<AggregateFieldInitTest<T, N>::template IsInitializable, 0, kFieldBlowupFactor * sizeof(T) + 1> {};

template<std::size_t kFieldInitCount, std::size_t kInitializerCount>
struct DetectSpecial
    : std::integral_constant<std::size_t, (kFieldInitCount > kInitializerCount ? 1 : kFieldInitCount)> {};

template<
    IsAggregate T,
    std::size_t kInitializerCount,
    std::size_t kFieldIndex,
    std::size_t kFieldCount,
    bool = (kFieldIndex < kInitializerCount && kFieldCount < kInitializerCount)>
struct AggregateFieldCounterImpl
    : AggregateFieldCounterImpl<
          T,
          kInitializerCount,
          kFieldIndex
              + DetectSpecial<AggregateFieldInitializerCount<T, kFieldIndex>::value, kInitializerCount - kFieldIndex>::
                  value,
          kFieldCount + 1> {};

template<IsAggregate T, std::size_t kInitializerCount, std::size_t kFieldIndex, std::size_t kFieldCount>
struct AggregateFieldCounterImpl<T, kInitializerCount, kFieldIndex, kFieldCount, false>
    : std::integral_constant<std::size_t, kFieldCount> {};

template<IsAggregate T, std::size_t kInitializerCount>
struct AggregateFieldCounter : AggregateFieldCounterImpl<T, kInitializerCount, 0, 0> {};

template<IsAggregate T>
struct AggregateFieldCounter<T, 0> : NotDecomposableImpl {};

template<IsAggregate T>
struct AggregateFieldCountRawImpl : AggregateFieldCounter<T, AggregateInitializerCount<T>::value> {};

// Only 1 base may have fields.
// If any base has fields, then T may not have fields itself. In other words
// there can only be one non empty base.

// a) Test whether `T` can be initialized from bases, all but one `field` empty.
//    If this works for one case, then there is exactly one non empty base.
template<IsAggregate T, std::size_t kNonEmptyIndex, typename BaseIndices>
struct OneNonEmptyBaseAtImpl;

template<IsAggregate T, std::size_t kNonEmptyIndex, std::size_t... kBaseIndex>
struct OneNonEmptyBaseAtImpl<T, kNonEmptyIndex, std::index_sequence<kBaseIndex...>>
    : IsBracesConstructibleImpl<T, AnyBaseMaybeEmptyN<kBaseIndex, T, kNonEmptyIndex != kBaseIndex>...> {};

template<IsAggregate T, std::size_t kFieldCount, std::size_t kNonEmptyIndex>
struct OneNonEmptyBaseAt : OneNonEmptyBaseAtImpl<T, kNonEmptyIndex, std::make_index_sequence<kFieldCount>> {};

template<IsAggregate T, std::size_t kFieldCount, typename Indices>
struct OneNonEmptyBaseIndicesImpl;

template<IsAggregate T, std::size_t kFieldCount, std::size_t... kIndex>
struct OneNonEmptyBaseIndicesImpl<T, kFieldCount, std::index_sequence<kIndex...>>
    : std::bool_constant<(... || OneNonEmptyBaseAt<T, kFieldCount, kIndex>::value)> {};

template<typename T, std::size_t kFieldCount>
concept OneNonEmptyBaseImpl =
    kFieldCount != NotDecomposableImpl::value
    && OneNonEmptyBaseIndicesImpl<T, kFieldCount, std::make_index_sequence<kFieldCount>>::value;

template<typename T>
concept OneNonEmptyBase = OneNonEmptyBaseImpl<T, AggregateFieldCountRawImpl<T>::value>;

// b) Test whether `T` can be initialized from at least one non empty base and
// at least one field.
//    In (a) only one non empty base was allowed, here we require at least one
//    non empty base.
template<IsAggregate T, std::size_t kNonEmptyIndex, typename BaseIndices, typename FieldIndices>
struct OneNonEmptyBasePlusFieldsAtImpl;

template<IsAggregate T, std::size_t kNonEmptyIndex, std::size_t... kBaseIndex, std::size_t... kFieldIndex>
struct OneNonEmptyBasePlusFieldsAtImpl<
    T,
    kNonEmptyIndex,
    std::index_sequence<kBaseIndex...>,
    std::index_sequence<kFieldIndex...>>
    : IsBracesConstructibleImpl<
          T,
          AnyBaseMaybeEmptyN<kBaseIndex, T, kNonEmptyIndex != kBaseIndex, true>...,
          AnyNonBaseTypeN<kFieldIndex, T>...> {};

template<IsAggregate T, std::size_t kFieldCount, std::size_t kBaseCount, std::size_t kNonEmptyIndex>
struct OneNonEmptyBasePlusFieldsAt
    : OneNonEmptyBasePlusFieldsAtImpl<
          T,
          kNonEmptyIndex,
          std::make_index_sequence<kBaseCount>,
          std::make_index_sequence<kFieldCount - kBaseCount>> {};

template<IsAggregate T, std::size_t kFieldCount, std::size_t kBaseCount, typename NonEmptyIndices>
struct OneNonEmptyBasePlusFieldsIndicesImpl;

template<IsAggregate T, std::size_t kFieldCount, std::size_t kBaseCount, std::size_t... kNonEmptyIndex>
struct OneNonEmptyBasePlusFieldsIndicesImpl<T, kFieldCount, kBaseCount, std::index_sequence<kNonEmptyIndex...>>
    : std::bool_constant<(... || OneNonEmptyBasePlusFieldsAt<T, kFieldCount, kBaseCount, kNonEmptyIndex>::value)> {};

template<typename T, std::size_t kFieldCount, std::size_t kBaseCount>
concept OneNonEmptyBasePlusFieldsIndices =
    OneNonEmptyBasePlusFieldsIndicesImpl<T, kFieldCount, kBaseCount, std::make_index_sequence<kBaseCount>>::value;

template<typename T, std::size_t kFieldCount, typename BaseCount>
struct OneNonEmptyBasePlusFieldsBaseCountImpl;

template<typename T, std::size_t kFieldCount, std::size_t... kBaseCount>
struct OneNonEmptyBasePlusFieldsBaseCountImpl<T, kFieldCount, std::index_sequence<kBaseCount...>>
    : std::bool_constant<(... || (OneNonEmptyBasePlusFieldsIndices<T, kFieldCount, kBaseCount>))> {};

template<typename T, std::size_t kFieldCount>
concept OneNonEmptyBasePlusFieldsBaseCount =
    kFieldCount > 0  // At least one field (non base)
    && kFieldCount
           != NotDecomposableImpl::value  // Prevent ~0 (a.k.a -1 or max)
               && OneNonEmptyBasePlusFieldsBaseCountImpl<T, kFieldCount, std::make_index_sequence<kFieldCount - 1>>::
                   value;

template<typename T>
concept OneNonEmptyBasePlusFields = OneNonEmptyBasePlusFieldsBaseCount<T, AggregateFieldCountRawImpl<T>::value>;

// c) Empty bases do not participate in structured binding/decomposition. So
// count them.
//    We actually count bases twice: 1) Only empty bases, b) any bases.

template<IsAggregate T, bool kRequireEmpty, typename BaseIndices, typename FieldIndices>
struct CountBasesTestImpl;

template<IsAggregate T, bool kRequireEmpty, std::size_t... kBaseIndex, std::size_t... kFieldIndex>
struct CountBasesTestImpl<T, kRequireEmpty, std::index_sequence<kBaseIndex...>, std::index_sequence<kFieldIndex...>>
    : std::conditional_t<
          IsBracesConstructibleImpl<
              T,
              AnyBaseMaybeEmptyN<kBaseIndex, T, kRequireEmpty>...,
              AnyNonBaseTypeN<kFieldIndex, T>...>::value,
          std::integral_constant<std::size_t, sizeof...(kBaseIndex)>,
          std::integral_constant<std::size_t, 0>> {};

template<IsAggregate T, bool kRequireEmpty, std::size_t kNumBases, std::size_t kNumFields>
struct CountBasesTest
    : CountBasesTestImpl<T, kRequireEmpty, std::make_index_sequence<kNumBases>, std::make_index_sequence<kNumFields>> {
};

template<IsAggregate T, bool kRequireEmpty, std::size_t kNumFields, typename NumBases>
struct CountBasesIndicesImpl;

template<IsAggregate T, bool kRequireEmpty, std::size_t kNumFields, std::size_t... kNumBases>
struct CountBasesIndicesImpl<T, kRequireEmpty, kNumFields, std::index_sequence<kNumBases...>>
    : std::integral_constant<
          std::size_t,
          (... + CountBasesTest<T, kRequireEmpty, kNumBases, kNumFields - kNumBases>::value)> {};

template<IsAggregate T, bool kRequireEmpty, std::size_t kNumFields>
struct CountBasesImpl : CountBasesIndicesImpl<T, kRequireEmpty, kNumFields, std::make_index_sequence<kNumFields>> {};

template<IsAggregate T, bool kRequireEmpty>
struct CountBases
    : std::conditional_t<
          AggregateFieldCountRawImpl<T>::value == NotDecomposableImpl::value,
          std::integral_constant<std::size_t, 0>,
          CountBasesImpl<T, kRequireEmpty, AggregateFieldCountRawImpl<T>::value>> {};

template<typename T>
concept HasMboTypesDecomposeCount = requires {
  { MboTypesDecomposeCount(static_cast<const std::remove_cvref_t<T>*>(nullptr)) } -> std::same_as<std::size_t>;
};

template<typename T>
consteval std::size_t GetMboTypesDecomposeCount() {
  if constexpr (HasMboTypesDecomposeCount<T>) {
    return MboTypesDecomposeCount(static_cast<const std::remove_cvref_t<T>*>(nullptr));
  } else {
    return NotDecomposableImpl::value;
  }
}

// Put all into once struct.
template<typename T, bool = IsAggregate<std::remove_cvref_t<T>> && !IsEmptyType<std::remove_cvref_t<T>>>
struct DecomposeInfo final {
  using Type = std::remove_cvref_t<T>;
  static constexpr bool kIsAggregate = IsAggregate<Type>;
  static constexpr bool kIsEmpty = IsEmptyType<Type>;
  static constexpr std::size_t kInitializerCount = AggregateInitializerCount<Type>::value;
  static constexpr std::size_t kFieldCount = AggregateFieldCountRawImpl<Type>::value;
  static constexpr bool kBadFieldCount =
      !kIsAggregate || kIsEmpty || kFieldCount == 0 || kFieldCount == NotDecomposableImpl::value;
  static constexpr bool kOneNonEmptyBase = OneNonEmptyBase<Type>;
  static constexpr bool kOneNonEmptyBasePlusFields = OneNonEmptyBasePlusFields<Type>;
  static constexpr std::size_t kCountBases = kBadFieldCount ? 0 : CountBases<Type, false>::value;
  static constexpr std::size_t kCountEmptyBases = kBadFieldCount ? 0 : CountBases<Type, true>::value;
  static constexpr bool kOnlyEmptyBases = kCountBases <= kCountEmptyBases;
  static constexpr std::size_t kCustomDecomposeCount = GetMboTypesDecomposeCount<Type>();
  static constexpr bool kDecomposable = kCustomDecomposeCount != NotDecomposableImpl::value
                                        || (!kBadFieldCount && kIsAggregate && !kIsEmpty
                                            && ((kOneNonEmptyBase || kOnlyEmptyBases) && !kOneNonEmptyBasePlusFields));
  static constexpr std::size_t kDecomposeCount =  // First check whether T is composable
      kCustomDecomposeCount != NotDecomposableImpl::value ? kCustomDecomposeCount
      : kDecomposable                             // format-NL
          ? (kCountBases + kCountEmptyBases == 0  // If it is, then check whether there are any bases.
                 ? kInitializerCount
                 : kFieldCount - kCountEmptyBases)
          : NotDecomposableImpl::value;

  static std::string Debug(std::string_view separator = ", ") {  // NOLINT(readability-function-cognitive-complexity)
    std::string str;
    std::string_view sep;
# define DEBUG_ADD(f)                                                                             \
   str += std::string(sep) + #f + ": "                                                            \
          + (std::same_as<std::remove_cvref_t<decltype(DecomposeInfo<T>::f)>, bool>               \
                 ? std::string(DecomposeInfo<T>::f ? "Yes" : "No")                                \
                 : (std::same_as<std::remove_cvref_t<decltype(DecomposeInfo<T>::f)>, std::size_t> \
                            && DecomposeInfo<T>::f == NotDecomposableImpl::value                  \
                        ? std::string("N/A")                                                      \
                        : std::to_string(DecomposeInfo<T>::f)));                                  \
   sep = separator
    DEBUG_ADD(kIsAggregate);
    DEBUG_ADD(kIsEmpty);
    DEBUG_ADD(kInitializerCount);
    DEBUG_ADD(kFieldCount);
    DEBUG_ADD(kBadFieldCount);
    DEBUG_ADD(kOneNonEmptyBase);
    DEBUG_ADD(kOneNonEmptyBasePlusFields);
    DEBUG_ADD(kOnlyEmptyBases);
    DEBUG_ADD(kDecomposable);
    DEBUG_ADD(kCountBases);
    DEBUG_ADD(kCountEmptyBases);
    DEBUG_ADD(kDecomposeCount);
# undef DEBUG_ADD
    return str;
  }
};

template<typename T>
struct DecomposeInfo<T, false> final {
  using Type = std::remove_cvref_t<T>;
  static constexpr bool kIsAggregate = IsAggregate<Type>;
  static constexpr bool kIsEmpty = IsEmptyType<Type>;
  static constexpr std::size_t kInitializerCount = 0;
  static constexpr std::size_t kFieldCount = 0;
  static constexpr bool kBadFieldCount = false;
  static constexpr bool kOneNonEmptyBase = false;
  static constexpr bool kOneNonEmptyBasePlusFields = false;
  static constexpr std::size_t kCountBases = 0;
  static constexpr std::size_t kCountEmptyBases = 0;
  static constexpr bool kOnlyEmptyBases = true;
  static constexpr bool kDecomposable = kIsAggregate && !kIsEmpty;
  static constexpr std::size_t kDecomposeCount = kDecomposable ? 0 : NotDecomposableImpl::value;

  static std::string Debug(std::string_view separator = ", ") {  // NOLINT(readability-function-cognitive-complexity)
    std::string str;
    std::string_view sep;
# define DEBUG_ADD(f)                                                                             \
   str += std::string(sep) + #f + ": "                                                            \
          + (std::same_as<std::remove_cvref_t<decltype(DecomposeInfo<T>::f)>, bool>               \
                 ? std::string(DecomposeInfo<T>::f ? "Yes" : "No")                                \
                 : (std::same_as<std::remove_cvref_t<decltype(DecomposeInfo<T>::f)>, std::size_t> \
                            && DecomposeInfo<T>::f == NotDecomposableImpl::value                  \
                        ? std::string("N/A")                                                      \
                        : std::to_string(DecomposeInfo<T>::f)));                                  \
   sep = separator
    DEBUG_ADD(kIsAggregate);
    DEBUG_ADD(kIsEmpty);
    // DEBUG_ADD(kInitializerCount);
    // DEBUG_ADD(kFieldCount);
    // DEBUG_ADD(kBadFieldCount);
    // DEBUG_ADD(kOneNonEmptyBase);
    // DEBUG_ADD(kOneNonEmptyBasePlusFields);
    // DEBUG_ADD(kOnlyEmptyBases);
    DEBUG_ADD(kDecomposable);
    // DEBUG_ADD(kCountBases);
    // DEBUG_ADD(kCountEmptyBases);
    DEBUG_ADD(kDecomposeCount);
# undef DEBUG_ADD
    return str;
  }
};

// What we actually need

template<typename T>
concept DecomposeCondition = DecomposeInfo<T>::kDecomposable;

template<typename T>
struct DecomposeCountImpl : std::integral_constant<std::size_t, DecomposeInfo<T>::kDecomposeCount> {};

#endif  // defined(__clang__)
// ----------------------------------------------------

// NOLINTBEGIN(readability-function-cognitive-complexity)

struct DecomposeHelper final {
  DecomposeHelper() = delete;

  template<std::size_t kForcedNumFields = kNotDecomposableValue, typename U>
  static constexpr auto ToAddressTuple(U& data) noexcept {
    using UR = std::remove_cvref_t<U>;
    constexpr bool kIsEmptyAggregate = IsAggregate<UR> && IsEmptyType<UR>;
    constexpr std::size_t kNumFields = kForcedNumFields != kNotDecomposableValue
                                           ? kForcedNumFields
                                           : (kIsEmptyAggregate ? 0 : DecomposeCountImpl<UR>::value);
    static_assert(kNumFields != kNotDecomposableValue);
    if constexpr (kNumFields == 0) {
      return std::make_tuple();
    } else if constexpr (kNumFields == 1) {
      auto& [a1] = data;
      if constexpr (requires { std::addressof(a1); }) {
        return std::make_tuple(std::addressof(a1));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 2) {
      auto& [a1, a2] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                    }) {
        return std::make_tuple(std::addressof(a1), std::addressof(a2));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 3) {
      auto& [a1, a2, a3] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                    }) {
        return std::make_tuple(std::addressof(a1), std::addressof(a2), std::addressof(a3));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 4) {
      auto& [a1, a2, a3, a4] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                    }) {
        return std::make_tuple(std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 5) {
      auto& [a1, a2, a3, a4, a5] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 6) {
      auto& [a1, a2, a3, a4, a5, a6] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 7) {
      auto& [a1, a2, a3, a4, a5, a6, a7] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 8) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 9) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 10) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 11) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 12) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 13) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 14) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 15) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 16) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 17) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 18) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 19) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 20) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 21) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21] = data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 22) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 23) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 24) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 25) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 26) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 27) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 28) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                      std::addressof(a28);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27), std::addressof(a28));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 29) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                      std::addressof(a28);
                      std::addressof(a29);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27), std::addressof(a28), std::addressof(a29));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 30) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                      std::addressof(a28);
                      std::addressof(a29);
                      std::addressof(a30);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27), std::addressof(a28), std::addressof(a29), std::addressof(a30));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 31) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                      std::addressof(a28);
                      std::addressof(a29);
                      std::addressof(a30);
                      std::addressof(a31);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27), std::addressof(a28), std::addressof(a29), std::addressof(a30),
            std::addressof(a31));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 32) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                      std::addressof(a28);
                      std::addressof(a29);
                      std::addressof(a30);
                      std::addressof(a31);
                      std::addressof(a32);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27), std::addressof(a28), std::addressof(a29), std::addressof(a30),
            std::addressof(a31), std::addressof(a32));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 33) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                      std::addressof(a28);
                      std::addressof(a29);
                      std::addressof(a30);
                      std::addressof(a31);
                      std::addressof(a32);
                      std::addressof(a33);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27), std::addressof(a28), std::addressof(a29), std::addressof(a30),
            std::addressof(a31), std::addressof(a32), std::addressof(a33));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 34) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                      std::addressof(a28);
                      std::addressof(a29);
                      std::addressof(a30);
                      std::addressof(a31);
                      std::addressof(a32);
                      std::addressof(a33);
                      std::addressof(a34);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27), std::addressof(a28), std::addressof(a29), std::addressof(a30),
            std::addressof(a31), std::addressof(a32), std::addressof(a33), std::addressof(a34));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 35) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                      std::addressof(a28);
                      std::addressof(a29);
                      std::addressof(a30);
                      std::addressof(a31);
                      std::addressof(a32);
                      std::addressof(a33);
                      std::addressof(a34);
                      std::addressof(a35);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27), std::addressof(a28), std::addressof(a29), std::addressof(a30),
            std::addressof(a31), std::addressof(a32), std::addressof(a33), std::addressof(a34), std::addressof(a35));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 36) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                      std::addressof(a28);
                      std::addressof(a29);
                      std::addressof(a30);
                      std::addressof(a31);
                      std::addressof(a32);
                      std::addressof(a33);
                      std::addressof(a34);
                      std::addressof(a35);
                      std::addressof(a36);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27), std::addressof(a28), std::addressof(a29), std::addressof(a30),
            std::addressof(a31), std::addressof(a32), std::addressof(a33), std::addressof(a34), std::addressof(a35),
            std::addressof(a36));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 37) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                      std::addressof(a28);
                      std::addressof(a29);
                      std::addressof(a30);
                      std::addressof(a31);
                      std::addressof(a32);
                      std::addressof(a33);
                      std::addressof(a34);
                      std::addressof(a35);
                      std::addressof(a36);
                      std::addressof(a37);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27), std::addressof(a28), std::addressof(a29), std::addressof(a30),
            std::addressof(a31), std::addressof(a32), std::addressof(a33), std::addressof(a34), std::addressof(a35),
            std::addressof(a36), std::addressof(a37));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 38) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                      std::addressof(a28);
                      std::addressof(a29);
                      std::addressof(a30);
                      std::addressof(a31);
                      std::addressof(a32);
                      std::addressof(a33);
                      std::addressof(a34);
                      std::addressof(a35);
                      std::addressof(a36);
                      std::addressof(a37);
                      std::addressof(a38);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27), std::addressof(a28), std::addressof(a29), std::addressof(a30),
            std::addressof(a31), std::addressof(a32), std::addressof(a33), std::addressof(a34), std::addressof(a35),
            std::addressof(a36), std::addressof(a37), std::addressof(a38));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 39) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                      std::addressof(a28);
                      std::addressof(a29);
                      std::addressof(a30);
                      std::addressof(a31);
                      std::addressof(a32);
                      std::addressof(a33);
                      std::addressof(a34);
                      std::addressof(a35);
                      std::addressof(a36);
                      std::addressof(a37);
                      std::addressof(a38);
                      std::addressof(a39);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27), std::addressof(a28), std::addressof(a29), std::addressof(a30),
            std::addressof(a31), std::addressof(a32), std::addressof(a33), std::addressof(a34), std::addressof(a35),
            std::addressof(a36), std::addressof(a37), std::addressof(a38), std::addressof(a39));
      } else {
        return std::make_tuple();
      }
    } else if constexpr (kNumFields == 40) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40] =
          data;
      if constexpr (requires {
                      std::addressof(a1);
                      std::addressof(a2);
                      std::addressof(a3);
                      std::addressof(a4);
                      std::addressof(a5);
                      std::addressof(a6);
                      std::addressof(a7);
                      std::addressof(a8);
                      std::addressof(a9);
                      std::addressof(a10);
                      std::addressof(a11);
                      std::addressof(a12);
                      std::addressof(a13);
                      std::addressof(a14);
                      std::addressof(a15);
                      std::addressof(a16);
                      std::addressof(a17);
                      std::addressof(a18);
                      std::addressof(a19);
                      std::addressof(a20);
                      std::addressof(a21);
                      std::addressof(a22);
                      std::addressof(a23);
                      std::addressof(a24);
                      std::addressof(a25);
                      std::addressof(a26);
                      std::addressof(a27);
                      std::addressof(a28);
                      std::addressof(a29);
                      std::addressof(a30);
                      std::addressof(a31);
                      std::addressof(a32);
                      std::addressof(a33);
                      std::addressof(a34);
                      std::addressof(a35);
                      std::addressof(a36);
                      std::addressof(a37);
                      std::addressof(a38);
                      std::addressof(a39);
                      std::addressof(a40);
                    }) {
        return std::make_tuple(
            std::addressof(a1), std::addressof(a2), std::addressof(a3), std::addressof(a4), std::addressof(a5),
            std::addressof(a6), std::addressof(a7), std::addressof(a8), std::addressof(a9), std::addressof(a10),
            std::addressof(a11), std::addressof(a12), std::addressof(a13), std::addressof(a14), std::addressof(a15),
            std::addressof(a16), std::addressof(a17), std::addressof(a18), std::addressof(a19), std::addressof(a20),
            std::addressof(a21), std::addressof(a22), std::addressof(a23), std::addressof(a24), std::addressof(a25),
            std::addressof(a26), std::addressof(a27), std::addressof(a28), std::addressof(a29), std::addressof(a30),
            std::addressof(a31), std::addressof(a32), std::addressof(a33), std::addressof(a34), std::addressof(a35),
            std::addressof(a36), std::addressof(a37), std::addressof(a38), std::addressof(a39), std::addressof(a40));
      } else {
        return std::make_tuple();
      }
    }
  }

  template<std::size_t kForcedNumFields = kNotDecomposableValue, typename U>
  static constexpr auto ToTuple(U&& data) noexcept {
    using UR = std::remove_cvref_t<U>;
    constexpr bool kIsEmptyAggregate = IsAggregate<UR> && IsEmptyType<UR>;
    constexpr std::size_t kNumFields = kForcedNumFields != kNotDecomposableValue
                                           ? kForcedNumFields
                                           : (kIsEmptyAggregate ? 0 : DecomposeCountImpl<UR>::value);
    static_assert(kNumFields != kNotDecomposableValue);
    if constexpr (kNumFields == 0) {
      return std::make_tuple();
    } else if constexpr (kNumFields == 1) {
      auto&& [a1] = std::forward<U>(data);
      return std::make_tuple(std::forward<decltype(a1)>(a1));
    } else if constexpr (kNumFields == 2) {
      auto&& [a1, a2] = std::forward<U>(data);
      return std::make_tuple(std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2));
    } else if constexpr (kNumFields == 3) {
      auto&& [a1, a2, a3] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3));
    } else if constexpr (kNumFields == 4) {
      auto&& [a1, a2, a3, a4] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4));
    } else if constexpr (kNumFields == 5) {
      auto&& [a1, a2, a3, a4, a5] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5));
    } else if constexpr (kNumFields == 6) {
      auto&& [a1, a2, a3, a4, a5, a6] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6));
    } else if constexpr (kNumFields == 7) {
      auto&& [a1, a2, a3, a4, a5, a6, a7] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7));
    } else if constexpr (kNumFields == 8) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8));
    } else if constexpr (kNumFields == 9) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9));
    } else if constexpr (kNumFields == 10) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10));
    } else if constexpr (kNumFields == 11) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11));
    } else if constexpr (kNumFields == 12) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12));
    } else if constexpr (kNumFields == 13) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13));
    } else if constexpr (kNumFields == 14) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14));
    } else if constexpr (kNumFields == 15) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15));
    } else if constexpr (kNumFields == 16) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16));
    } else if constexpr (kNumFields == 17) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17));
    } else if constexpr (kNumFields == 18) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18] = std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18));
    } else if constexpr (kNumFields == 19) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19));
    } else if constexpr (kNumFields == 20) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20));
    } else if constexpr (kNumFields == 21) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21));
    } else if constexpr (kNumFields == 22) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22));
    } else if constexpr (kNumFields == 23) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23));
    } else if constexpr (kNumFields == 24) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24));
    } else if constexpr (kNumFields == 25) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25));
    } else if constexpr (kNumFields == 26) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26));
    } else if constexpr (kNumFields == 27) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27));
    } else if constexpr (kNumFields == 28) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27),
          std::forward<decltype(a28)>(a28));
    } else if constexpr (kNumFields == 29) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27),
          std::forward<decltype(a28)>(a28), std::forward<decltype(a29)>(a29));
    } else if constexpr (kNumFields == 30) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27),
          std::forward<decltype(a28)>(a28), std::forward<decltype(a29)>(a29), std::forward<decltype(a30)>(a30));
    } else if constexpr (kNumFields == 31) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27),
          std::forward<decltype(a28)>(a28), std::forward<decltype(a29)>(a29), std::forward<decltype(a30)>(a30),
          std::forward<decltype(a31)>(a31));
    } else if constexpr (kNumFields == 32) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27),
          std::forward<decltype(a28)>(a28), std::forward<decltype(a29)>(a29), std::forward<decltype(a30)>(a30),
          std::forward<decltype(a31)>(a31), std::forward<decltype(a32)>(a32));
    } else if constexpr (kNumFields == 33) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27),
          std::forward<decltype(a28)>(a28), std::forward<decltype(a29)>(a29), std::forward<decltype(a30)>(a30),
          std::forward<decltype(a31)>(a31), std::forward<decltype(a32)>(a32), std::forward<decltype(a33)>(a33));
    } else if constexpr (kNumFields == 34) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27),
          std::forward<decltype(a28)>(a28), std::forward<decltype(a29)>(a29), std::forward<decltype(a30)>(a30),
          std::forward<decltype(a31)>(a31), std::forward<decltype(a32)>(a32), std::forward<decltype(a33)>(a33),
          std::forward<decltype(a34)>(a34));
    } else if constexpr (kNumFields == 35) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27),
          std::forward<decltype(a28)>(a28), std::forward<decltype(a29)>(a29), std::forward<decltype(a30)>(a30),
          std::forward<decltype(a31)>(a31), std::forward<decltype(a32)>(a32), std::forward<decltype(a33)>(a33),
          std::forward<decltype(a34)>(a34), std::forward<decltype(a35)>(a35));
    } else if constexpr (kNumFields == 36) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27),
          std::forward<decltype(a28)>(a28), std::forward<decltype(a29)>(a29), std::forward<decltype(a30)>(a30),
          std::forward<decltype(a31)>(a31), std::forward<decltype(a32)>(a32), std::forward<decltype(a33)>(a33),
          std::forward<decltype(a34)>(a34), std::forward<decltype(a35)>(a35), std::forward<decltype(a36)>(a36));
    } else if constexpr (kNumFields == 37) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27),
          std::forward<decltype(a28)>(a28), std::forward<decltype(a29)>(a29), std::forward<decltype(a30)>(a30),
          std::forward<decltype(a31)>(a31), std::forward<decltype(a32)>(a32), std::forward<decltype(a33)>(a33),
          std::forward<decltype(a34)>(a34), std::forward<decltype(a35)>(a35), std::forward<decltype(a36)>(a36),
          std::forward<decltype(a37)>(a37));
    } else if constexpr (kNumFields == 38) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27),
          std::forward<decltype(a28)>(a28), std::forward<decltype(a29)>(a29), std::forward<decltype(a30)>(a30),
          std::forward<decltype(a31)>(a31), std::forward<decltype(a32)>(a32), std::forward<decltype(a33)>(a33),
          std::forward<decltype(a34)>(a34), std::forward<decltype(a35)>(a35), std::forward<decltype(a36)>(a36),
          std::forward<decltype(a37)>(a37), std::forward<decltype(a38)>(a38));
    } else if constexpr (kNumFields == 39) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27),
          std::forward<decltype(a28)>(a28), std::forward<decltype(a29)>(a29), std::forward<decltype(a30)>(a30),
          std::forward<decltype(a31)>(a31), std::forward<decltype(a32)>(a32), std::forward<decltype(a33)>(a33),
          std::forward<decltype(a34)>(a34), std::forward<decltype(a35)>(a35), std::forward<decltype(a36)>(a36),
          std::forward<decltype(a37)>(a37), std::forward<decltype(a38)>(a38), std::forward<decltype(a39)>(a39));
    } else if constexpr (kNumFields == 40) {
      auto&& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40] =
          std::forward<U>(data);
      return std::make_tuple(
          std::forward<decltype(a1)>(a1), std::forward<decltype(a2)>(a2), std::forward<decltype(a3)>(a3),
          std::forward<decltype(a4)>(a4), std::forward<decltype(a5)>(a5), std::forward<decltype(a6)>(a6),
          std::forward<decltype(a7)>(a7), std::forward<decltype(a8)>(a8), std::forward<decltype(a9)>(a9),
          std::forward<decltype(a10)>(a10), std::forward<decltype(a11)>(a11), std::forward<decltype(a12)>(a12),
          std::forward<decltype(a13)>(a13), std::forward<decltype(a14)>(a14), std::forward<decltype(a15)>(a15),
          std::forward<decltype(a16)>(a16), std::forward<decltype(a17)>(a17), std::forward<decltype(a18)>(a18),
          std::forward<decltype(a19)>(a19), std::forward<decltype(a20)>(a20), std::forward<decltype(a21)>(a21),
          std::forward<decltype(a22)>(a22), std::forward<decltype(a23)>(a23), std::forward<decltype(a24)>(a24),
          std::forward<decltype(a25)>(a25), std::forward<decltype(a26)>(a26), std::forward<decltype(a27)>(a27),
          std::forward<decltype(a28)>(a28), std::forward<decltype(a29)>(a29), std::forward<decltype(a30)>(a30),
          std::forward<decltype(a31)>(a31), std::forward<decltype(a32)>(a32), std::forward<decltype(a33)>(a33),
          std::forward<decltype(a34)>(a34), std::forward<decltype(a35)>(a35), std::forward<decltype(a36)>(a36),
          std::forward<decltype(a37)>(a37), std::forward<decltype(a38)>(a38), std::forward<decltype(a39)>(a39),
          std::forward<decltype(a40)>(a40));
    }
  }

  template<std::size_t kForcedNumFields = kNotDecomposableValue, typename U>
  static constexpr auto ToTuple(U& data) noexcept {
    using UR = std::remove_cvref_t<U>;
    constexpr bool kIsEmptyAggregate = IsAggregate<UR> && IsEmptyType<UR>;
    constexpr std::size_t kNumFields = kForcedNumFields != kNotDecomposableValue
                                           ? kForcedNumFields
                                           : (kIsEmptyAggregate ? 0 : DecomposeCountImpl<UR>::value);
    static_assert(kNumFields != kNotDecomposableValue);
    if constexpr (kNumFields == 0) {
      return std::make_tuple();
    } else if constexpr (kNumFields == 1) {
      auto& [a1] = data;
      return std::tie(a1);
    } else if constexpr (kNumFields == 2) {
      auto& [a1, a2] = data;
      return std::tie(a1, a2);
    } else if constexpr (kNumFields == 3) {
      auto& [a1, a2, a3] = data;
      return std::tie(a1, a2, a3);
    } else if constexpr (kNumFields == 4) {
      auto& [a1, a2, a3, a4] = data;
      return std::tie(a1, a2, a3, a4);
    } else if constexpr (kNumFields == 5) {
      auto& [a1, a2, a3, a4, a5] = data;
      return std::tie(a1, a2, a3, a4, a5);
    } else if constexpr (kNumFields == 6) {
      auto& [a1, a2, a3, a4, a5, a6] = data;
      return std::tie(a1, a2, a3, a4, a5, a6);
    } else if constexpr (kNumFields == 7) {
      auto& [a1, a2, a3, a4, a5, a6, a7] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7);
    } else if constexpr (kNumFields == 8) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8);
    } else if constexpr (kNumFields == 9) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9);
    } else if constexpr (kNumFields == 10) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    } else if constexpr (kNumFields == 11) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
    } else if constexpr (kNumFields == 12) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
    } else if constexpr (kNumFields == 13) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
    } else if constexpr (kNumFields == 14) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
    } else if constexpr (kNumFields == 15) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15);
    } else if constexpr (kNumFields == 16) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
    } else if constexpr (kNumFields == 17) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17);
    } else if constexpr (kNumFields == 18) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18);
    } else if constexpr (kNumFields == 19) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19);
    } else if constexpr (kNumFields == 20) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20);
    } else if constexpr (kNumFields == 21) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21);
    } else if constexpr (kNumFields == 22) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22);
    } else if constexpr (kNumFields == 23) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23);
    } else if constexpr (kNumFields == 24) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23,
          a24);
    } else if constexpr (kNumFields == 25) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25);
    } else if constexpr (kNumFields == 26) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26);
    } else if constexpr (kNumFields == 27) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27);
    } else if constexpr (kNumFields == 28) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28);
    } else if constexpr (kNumFields == 29) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29);
    } else if constexpr (kNumFields == 30) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30);
    } else if constexpr (kNumFields == 31) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31);
    } else if constexpr (kNumFields == 32) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32);
    } else if constexpr (kNumFields == 33) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33);
    } else if constexpr (kNumFields == 34) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34);
    } else if constexpr (kNumFields == 35) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35);
    } else if constexpr (kNumFields == 36) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36);
    } else if constexpr (kNumFields == 37) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37);
    } else if constexpr (kNumFields == 38) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38);
    } else if constexpr (kNumFields == 39) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39);
    } else if constexpr (kNumFields == 40) {
      auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40);
    }
  }

  template<std::size_t kForcedNumFields = kNotDecomposableValue, typename U>
  static constexpr auto ToTuple(const U& data) noexcept {
    using UR = std::remove_cvref_t<U>;
    constexpr bool kIsEmptyAggregate = IsAggregate<UR> && IsEmptyType<UR>;
    constexpr std::size_t kNumFields = kForcedNumFields != kNotDecomposableValue
                                           ? kForcedNumFields
                                           : (kIsEmptyAggregate ? 0 : DecomposeCountImpl<UR>::value);
    static_assert(kNumFields != kNotDecomposableValue);
    if constexpr (kNumFields == 0) {
      return std::make_tuple();
    } else if constexpr (kNumFields == 1) {
      const auto& [a1] = data;
      return std::tie(a1);
    } else if constexpr (kNumFields == 2) {
      const auto& [a1, a2] = data;
      return std::tie(a1, a2);
    } else if constexpr (kNumFields == 3) {
      const auto& [a1, a2, a3] = data;
      return std::tie(a1, a2, a3);
    } else if constexpr (kNumFields == 4) {
      const auto& [a1, a2, a3, a4] = data;
      return std::tie(a1, a2, a3, a4);
    } else if constexpr (kNumFields == 5) {
      const auto& [a1, a2, a3, a4, a5] = data;
      return std::tie(a1, a2, a3, a4, a5);
    } else if constexpr (kNumFields == 6) {
      const auto& [a1, a2, a3, a4, a5, a6] = data;
      return std::tie(a1, a2, a3, a4, a5, a6);
    } else if constexpr (kNumFields == 7) {
      const auto& [a1, a2, a3, a4, a5, a6, a7] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7);
    } else if constexpr (kNumFields == 8) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8);
    } else if constexpr (kNumFields == 9) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9);
    } else if constexpr (kNumFields == 10) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    } else if constexpr (kNumFields == 11) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
    } else if constexpr (kNumFields == 12) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
    } else if constexpr (kNumFields == 13) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
    } else if constexpr (kNumFields == 14) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
    } else if constexpr (kNumFields == 15) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15);
    } else if constexpr (kNumFields == 16) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
    } else if constexpr (kNumFields == 17) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17);
    } else if constexpr (kNumFields == 18) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18);
    } else if constexpr (kNumFields == 19) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19);
    } else if constexpr (kNumFields == 20) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20] = data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20);
    } else if constexpr (kNumFields == 21) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21] =
          data;
      return std::tie(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21);
    } else if constexpr (kNumFields == 22) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22);
    } else if constexpr (kNumFields == 23) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23);
    } else if constexpr (kNumFields == 24) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23,
          a24);
    } else if constexpr (kNumFields == 25) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25);
    } else if constexpr (kNumFields == 26) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26);
    } else if constexpr (kNumFields == 27) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27);
    } else if constexpr (kNumFields == 28) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28);
    } else if constexpr (kNumFields == 29) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29);
    } else if constexpr (kNumFields == 30) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30);
    } else if constexpr (kNumFields == 31) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31);
    } else if constexpr (kNumFields == 32) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32);
    } else if constexpr (kNumFields == 33) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33);
    } else if constexpr (kNumFields == 34) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34);
    } else if constexpr (kNumFields == 35) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35);
    } else if constexpr (kNumFields == 36) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36);
    } else if constexpr (kNumFields == 37) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37);
    } else if constexpr (kNumFields == 38) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38);
    } else if constexpr (kNumFields == 39) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39);
    } else if constexpr (kNumFields == 40) {
      const auto& [a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40] =
          data;
      return std::tie(
          a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24,
          a25, a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39, a40);
    }
  }
};

// NOLINTEND(readability-function-cognitive-complexity)
// NOLINTEND(*-magic-numbers)

}  // namespace mbo::types::types_internal

#endif  // MBO_TYPES_INTERNAL_DECOMPOSE_COUNT_H_
