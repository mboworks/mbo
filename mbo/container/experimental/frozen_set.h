// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_EXPERIMENTAL_FROZEN_SET_H_
#define MBO_CONTAINER_EXPERIMENTAL_FROZEN_SET_H_

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <utility>

#include "mbo/container/experimental/frozen_options.h"  // IWYU pragma: export
#include "mbo/container/experimental/internal/frozen_table.h"
#include "mbo/types/optional_ref.h"

namespace mbo::container::experimental {

// NOLINTBEGIN(readability-identifier-naming): standard associative container interface.

template<typename Key, auto CapacityOrOptions, typename Hash = FrozenHash<Key>, typename KeyEqual = std::equal_to<>>
class FrozenSet final : public frozen_internal::FrozenTable<Key, Key, CapacityOrOptions, Hash, KeyEqual> {
  using Base = frozen_internal::FrozenTable<Key, Key, CapacityOrOptions, Hash, KeyEqual>;

 public:
  using Base::Base;

  [[nodiscard]] constexpr mbo::types::OptionalRef<const Key> lookup(const Key& key) const { return this->Lookup(key); }

  template<typename K>
  requires(Base::template kForeign<K>)
  [[nodiscard]] constexpr mbo::types::OptionalRef<const Key> lookup(const K& key) const {
    return this->Lookup(key);
  }
};

template<
    typename Key,
    auto LeftOptions,
    auto RightOptions,
    typename LeftHash,
    typename RightHash,
    typename LeftEqual,
    typename RightEqual>
requires std::equality_comparable<Key>
constexpr bool operator==(
    const FrozenSet<Key, LeftOptions, LeftHash, LeftEqual>& lhs,
    const FrozenSet<Key, RightOptions, RightHash, RightEqual>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  return std::ranges::all_of(lhs, [&rhs](const auto& key) {
    const auto found = rhs.find(key);
    return found != rhs.end() && *found == key;
  });
}

template<typename Key, std::size_t N, typename Hash = FrozenHash<Key>, typename KeyEqual = std::equal_to<>>
FrozenSet(const std::array<Key, N>&, Hash = {}, KeyEqual = {}) -> FrozenSet<Key, N, Hash, KeyEqual>;

template<typename Key, std::size_t N, typename Hash = FrozenHash<Key>, typename KeyEqual = std::equal_to<>>
constexpr auto MakeFrozenSet(const std::array<Key, N>& values, Hash hash = {}, KeyEqual equal = {}) {
  return FrozenSet<Key, N, Hash, KeyEqual>(values, std::move(hash), std::move(equal));
}

template<typename Key, std::size_t N, typename Hash = FrozenHash<Key>, typename KeyEqual = std::equal_to<>>
constexpr auto ToFrozenSet(const std::array<Key, N>& values, Hash hash = {}, KeyEqual equal = {}) {
  return MakeFrozenSet(values, std::move(hash), std::move(equal));
}

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::experimental

#endif  // MBO_CONTAINER_EXPERIMENTAL_FROZEN_SET_H_
