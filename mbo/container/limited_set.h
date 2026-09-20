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

#ifndef MBO_CONTAINER_LIMITED_SET_H_
#define MBO_CONTAINER_LIMITED_SET_H_

#include <algorithm>
#include <compare>   // IWYU pragma: keep
#include <concepts>  // IWYU pragma: keep
#include <initializer_list>
#include <memory>  // IWYU pragma: keep
#include <new>     // IWYU pragma: keep
#include <type_traits>
#include <utility>

#include "mbo/container/internal/limited_ordered.h"  // IWYU pragma: export
#include "mbo/types/compare.h"                       // IWYU pragma: keep
#include "mbo/types/traits.h"

namespace mbo::container {

// NOLINTBEGIN(readability-identifier-naming)

// Implements a `std::set` like container that only uses inlined memory. So if used as a local
// variable with a types that does not perform memory allocation, then this type does not perform
// any memory allocation.
//
// Unlike `std::array` this type can vary in size.
//
// The type is fully `constexpr` compliant and can be used as a `static constexpr`.
//
// Can be constructed with helpers `MakeLimitedSet` or `ToLimitedSet`.
//
// Example:
//
// ```c++
// using mbo::container::LimitedSet;
//
// constexpr auto kMyData = MakeLimitedSet(1, 3, 2, 4);
// ```
//
// The above example infers the value_type to be `int` as it is the common type of the arguments.
// The resulting `LimitedSet` has a capacity of 4 and the elements {1, 2, 3, 4}.
//
// Internally slot storage is used and elements are moved as needed. That means
// that element addresses are not stable. Iterators are random access but not
// contiguous, there is no `data()` member, and every iterator exposes keys as
// const so callers cannot invalidate the ordering.
//
// Insertions and erasures provide the basic exception guarantee. If relocating
// an existing element throws, the container remains valid and destructible but
// may be empty.
//
// Note that construction from char-arrays results in a container of `std::string_view` as opposed to `const char*`.
template<typename Key, auto CapacityOrOptions, typename Compare = std::less<Key>>
requires(std::move_constructible<Key>)
class LimitedSet final : public container_internal::LimitedOrdered<Key, Key, Key, CapacityOrOptions, Compare> {
  using LimitedBase = container_internal::LimitedOrdered<Key, Key, Key, CapacityOrOptions, Compare>;
  static constexpr std::size_t kCapacity = MakeLimitedOptions<CapacityOrOptions>().kCapacity;

 public:
  using container_internal::LimitedOrdered<Key, Key, Key, CapacityOrOptions, Compare>::LimitedOrdered;

  constexpr ~LimitedSet() noexcept = default;

  constexpr explicit LimitedSet(const Compare& key_comp) : LimitedBase(key_comp) {}

  constexpr LimitedSet(const LimitedSet& other) : LimitedBase(other) {}

  constexpr LimitedSet& operator=(const LimitedSet& other) {
    if (this != &other) {
      LimitedBase::operator=(other);
    }
    return *this;
  }

  // The base move may propagate comparator or element exceptions.
  // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor)
  constexpr LimitedSet(LimitedSet&& other) : LimitedBase(std::move(other)) {}

  // The base move may propagate comparator or element exceptions.
  // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations,performance-noexcept-move-constructor)
  constexpr LimitedSet& operator=(LimitedSet&& other) {
    if (this != &other) {
      LimitedBase::operator=(std::move(other));
    }
    return *this;
  }

  // Constructors and assignment from other LimitSet/value types.

  template<std::forward_iterator It>
  requires types::ConstructibleFrom<Key, mbo::types::ForwardIteratorValueType<It>>
  constexpr LimitedSet(It begin, It end, const Compare& key_comp = Compare()) : LimitedBase(begin, end, key_comp) {}

  constexpr LimitedSet(const std::initializer_list<Key>& list, const Compare& key_comp = Compare())
      : LimitedBase(list, key_comp) {}

  template<types::ConstructibleInto<Key> U>
  requires(!std::same_as<U, Key>)
  constexpr LimitedSet(const std::initializer_list<U>& list, const Compare& key_comp = Compare())
      : LimitedBase(list, key_comp) {}

  template<types::ConstructibleInto<Key> U, auto OtherN>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= kCapacity)
  constexpr LimitedSet& operator=(const std::initializer_list<U>& list) {
    LimitedBase::operator=(list);
    return *this;
  }

  template<types::ConstructibleInto<Key> OK, auto OtherN, typename OtherCompare>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= kCapacity)
  constexpr explicit LimitedSet(const LimitedSet<OK, OtherN, OtherCompare>& other) : LimitedBase(other) {}

  template<types::ConstructibleInto<Key> OK, auto OtherN, typename OtherCompare>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= kCapacity)
  constexpr LimitedSet& operator=(const LimitedSet<OK, OtherN, OtherCompare>& other) {
    LimitedBase::operator=(other);
    return *this;
  }

  template<types::ConstructibleInto<Key> OK, auto OtherN, typename OtherCompare>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= kCapacity)
  constexpr explicit LimitedSet(LimitedSet<OK, OtherN, OtherCompare>&& other) : LimitedBase(std::move(other)) {}

  // Find and search: lower_bound, upper_bound, equal_range, find, contains, count

  using LimitedBase::contains;
  using LimitedBase::contains_all;
  using LimitedBase::contains_any;
  using LimitedBase::count;
  using LimitedBase::equal_range;
  using LimitedBase::find;
  using LimitedBase::lower_bound;
  using LimitedBase::upper_bound;

  // Custom

  using LimitedBase::at_index;  // Index based access, throws `std::out_of_range` if not found.
  using LimitedBase::index_of;  // Return 0-based index of `key`, or `npos` if not found.
  using LimitedBase::npos;      // Return value for `index_of` if `key` is not found.

  // Modification: clear, swap, emplace, insert

  using LimitedBase::clear;
  using LimitedBase::emplace;
  using LimitedBase::erase;
  using LimitedBase::insert;
  using LimitedBase::swap;

  // Read/write access

  using LimitedBase::capacity;
  using LimitedBase::empty;
  using LimitedBase::max_size;
  using LimitedBase::size;

  using LimitedBase::begin;
  using LimitedBase::cbegin;
  using LimitedBase::cend;
  using LimitedBase::crbegin;
  using LimitedBase::crend;
  using LimitedBase::end;
  using LimitedBase::rbegin;
  using LimitedBase::rend;

  // Observers

  using LimitedBase::key_comp;
  using LimitedBase::value_comp;
};

template<types::NotIsCharArray... T>
LimitedSet(T&&... data) -> LimitedSet<std::common_type_t<T...>, sizeof...(T)>;

template<types::IsCharArray... T>
LimitedSet(T&&... data) -> LimitedSet<std::string_view, sizeof...(T)>;

template<auto LN, auto RN, typename LHS, typename RHS, typename LCompare, typename RCompare>
requires std::three_way_comparable_with<LHS, RHS>
constexpr inline auto operator<=>(const LimitedSet<LHS, LN, LCompare>& lhs, const LimitedSet<RHS, RN, RCompare>& rhs) {
  auto lhs_it = lhs.begin();
  auto rhs_it = rhs.begin();
  while (lhs_it != lhs.end() && rhs_it != rhs.end()) {
    const auto comp = *lhs_it <=> *rhs_it;
    if (comp != 0) {
      return comp;
    }
    ++lhs_it;
    ++rhs_it;
  }
  return lhs.size() <=> rhs.size();
}

template<auto LN, auto RN, typename LHS, typename RHS, typename LCompare, typename RCompare>
requires std::three_way_comparable_with<LHS, RHS>
constexpr inline bool operator==(const LimitedSet<LHS, LN, LCompare>& lhs, const LimitedSet<RHS, RN, RCompare>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  auto lhs_it = lhs.begin();
  auto rhs_it = rhs.begin();
  while (lhs_it != lhs.end() && rhs_it != rhs.end()) {
    const auto comp = *lhs_it <=> *rhs_it;
    if (comp != 0) {
      return false;
    }
    ++lhs_it;
    ++rhs_it;
  }
  return true;
}

template<auto LN, auto RN, typename LHS, typename RHS, typename LCompare, typename RCompare>
requires std::three_way_comparable_with<LHS, RHS>
constexpr inline bool operator<(const LimitedSet<LHS, LN, LCompare>& lhs, const LimitedSet<RHS, RN, RCompare>& rhs) {
  auto lhs_it = lhs.begin();
  auto rhs_it = rhs.begin();
  while (lhs_it != lhs.end() && rhs_it != rhs.end()) {
    const auto comp = *lhs_it <=> *rhs_it;
    if (comp != 0) {
      return comp < 0;
    }
    ++lhs_it;
    ++rhs_it;
  }
  return lhs.size() < rhs.size();
}

template<typename Key, typename... Args>
requires((types::NotInitializerList<Args> && types::ConstructibleFrom<Key, Args>) && ...)
inline constexpr auto MakeLimitedSetOf(Args&&... args) {
  auto result = LimitedSet<Key, sizeof...(Args)>();
  (result.emplace(std::forward<Args>(args)), ...);
  return result;
}

template<typename Key, auto N = 0, typename Compare = std::less<Key>>
inline constexpr auto MakeLimitedSet() {  // Parameter `key_comp` would create a conflict.
  return LimitedSet<Key, N, Compare>();
}

template<auto N, std::forward_iterator It, typename Compare = std::less<mbo::types::ForwardIteratorValueType<It>>>
inline constexpr auto MakeLimitedSet(It begin, It end, const Compare& key_comp = Compare()) {
  return LimitedSet<mbo::types::ForwardIteratorValueType<It>, N, Compare>(begin, end, key_comp);
}

template<auto N, typename Key, typename Compare = std::less<Key>>
inline constexpr auto MakeLimitedSet(const std::initializer_list<Key>& data, const Compare& key_comp = Compare()) {
  return LimitedSet<Key, N, Compare>(data, key_comp);
}

template<typename... Args>
requires((types::NotInitializerList<Args> && !std::forward_iterator<Args> && !types::IsCharArray<Args>) && ...)
inline constexpr auto MakeLimitedSet(Args&&... args) {
  using T = std::common_type_t<Args...>;
  auto result = LimitedSet<T, sizeof...(Args)>();
  (result.emplace(std::forward<T>(args)), ...);
  return result;
}

// This specialization takes `const char*` and `const char(&)[N]` arguments and creates an appropriately sized container
// of type `LimitedSet<std::string_view>`.
template<int&..., types::IsCharArray... Args>
inline constexpr auto MakeLimitedSet(Args... args) {
  auto result = LimitedSet<std::string_view, sizeof...(Args)>();
  (result.emplace(std::string_view(args)), ...);
  return result;
}

template<types::NotIsCharArray Key, int&..., types::IsCharArray... Args>
inline constexpr auto MakeLimitedSet(Args... args) {
  auto result = LimitedSet<Key, sizeof...(Args)>();
  (result.emplace(Key(args)), ...);
  return result;
}

// NOLINTBEGIN(*-avoid-c-arrays)
template<typename Key, std::size_t N, typename Compare = std::less<Key>>
constexpr LimitedSet<std::remove_cvref_t<Key>, N, Compare> ToLimitedSet(
    Key (&array)[N],
    const Compare& key_comp = Compare()) {
  LimitedSet<std::remove_cvref_t<Key>, N, Compare> result(key_comp);
  for (std::size_t idx = 0; idx < N; ++idx) {
    result.emplace(array[idx]);
  }
  return result;
}

// NOLINTEND(*-avoid-c-arrays)

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container

#endif  // MBO_CONTAINER_LIMITED_SET_H_
