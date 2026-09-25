// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_COLLISION_H_
#define MBO_CONTAINER_INTERNAL_HAMT_COLLISION_H_

#include <concepts>
#include <functional>
#include <iterator>

namespace mbo::container::container_internal {

template<
    std::forward_iterator Iterator,
    typename Hash,
    typename LookupKey,
    typename HashOf,
    typename KeyOf,
    typename Equal>
requires requires(Iterator pos, const LookupKey& key, HashOf hash_of, KeyOf key_of, Equal equal) {
  { std::invoke(hash_of, *pos) } noexcept -> std::convertible_to<Hash>;
  { std::invoke(equal, std::invoke(key_of, *pos), key) } noexcept -> std::convertible_to<bool>;
}
constexpr Iterator FindHamtCollision(
    Iterator first,
    Iterator last,
    Hash hash,
    const LookupKey& key,
    HashOf hash_of,
    KeyOf key_of,
    Equal equal) noexcept {
  for (; first != last; ++first) {
    if (std::invoke(hash_of, *first) == hash && std::invoke(equal, std::invoke(key_of, *first), key)) {
      break;
    }
  }
  return first;
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_COLLISION_H_
