// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_EXPERIMENTAL_FROZEN_MAP_H_
#define MBO_CONTAINER_EXPERIMENTAL_FROZEN_MAP_H_

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

#include "mbo/config/require.h"
#include "mbo/container/experimental/frozen_options.h"  // IWYU pragma: export
#include "mbo/container/experimental/internal/frozen_table.h"
#include "mbo/types/optional_ref.h"

namespace mbo::container::experimental {

// NOLINTBEGIN(readability-identifier-naming): standard associative container interface.

// Immutable, inline, constexpr perfect hash map. See FROZEN.md for the C++26 interface boundary.
template<
    typename Key,
    typename Value,
    auto CapacityOrOptions,
    typename Hash = FrozenHash<Key>,
    typename KeyEqual = std::equal_to<>>
class FrozenMap final
    : public frozen_internal::FrozenTable<Key, std::pair<const Key, Value>, CapacityOrOptions, Hash, KeyEqual> {
  static_assert(std::is_object_v<Value>, "Frozen mapped values must be object types");
  using Base = frozen_internal::FrozenTable<Key, std::pair<const Key, Value>, CapacityOrOptions, Hash, KeyEqual>;

 public:
  using mapped_type = Value;
  using Base::Base;

  [[nodiscard]] constexpr mbo::types::OptionalRef<const Value> lookup(const Key& key) const {
    return this->Lookup(key);
  }

  template<typename K>
  requires(Base::template kForeign<K>)
  [[nodiscard]] constexpr mbo::types::OptionalRef<const Value> lookup(const K& key) const {
    return this->Lookup(key);
  }

  constexpr const Value& at(const Key& key) const { return At(key); }

  template<typename K>
  requires(Base::template kForeign<K>)
  constexpr const Value& at(const K& key) const {
    return At(key);
  }

 private:
  template<typename K>
  constexpr const Value& At(const K& key) const {
    const auto found = lookup(key);
    MBO_CONFIG_REQUIRE(found.has_value(), "FrozenMap key not found");
    return *found;
  }
};

template<
    typename Key,
    typename Value,
    auto LeftOptions,
    auto RightOptions,
    typename LeftHash,
    typename RightHash,
    typename LeftEqual,
    typename RightEqual>
requires(std::equality_comparable<Key> && std::equality_comparable<Value>)
constexpr bool operator==(
    const FrozenMap<Key, Value, LeftOptions, LeftHash, LeftEqual>& lhs,
    const FrozenMap<Key, Value, RightOptions, RightHash, RightEqual>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  return std::ranges::all_of(lhs, [&rhs](const auto& entry) {
    const auto found = rhs.find(entry.first);
    return found != rhs.end() && found->first == entry.first && found->second == entry.second;
  });
}

template<
    typename Key,
    typename Value,
    std::size_t N,
    typename Hash = FrozenHash<std::remove_const_t<Key>>,
    typename KeyEqual = std::equal_to<>>
FrozenMap(const std::array<std::pair<Key, Value>, N>&, Hash = {}, KeyEqual = {})
    -> FrozenMap<std::remove_const_t<Key>, Value, N, Hash, KeyEqual>;

// Array factories infer capacity; options can instead be supplied to the class template explicitly.
template<
    typename Key,
    typename Value,
    std::size_t N,
    typename Hash = FrozenHash<std::remove_const_t<Key>>,
    typename KeyEqual = std::equal_to<>>
constexpr auto MakeFrozenMap(const std::array<std::pair<Key, Value>, N>& values, Hash hash = {}, KeyEqual equal = {}) {
  return FrozenMap<std::remove_const_t<Key>, Value, N, Hash, KeyEqual>(values, std::move(hash), std::move(equal));
}

template<
    typename Key,
    typename Value,
    std::size_t N,
    typename Hash = FrozenHash<std::remove_const_t<Key>>,
    typename KeyEqual = std::equal_to<>>
constexpr auto ToFrozenMap(const std::array<std::pair<Key, Value>, N>& values, Hash hash = {}, KeyEqual equal = {}) {
  return MakeFrozenMap(values, std::move(hash), std::move(equal));
}

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::experimental

#endif  // MBO_CONTAINER_EXPERIMENTAL_FROZEN_MAP_H_
