// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_COLLISION_H_
#define MBO_CONTAINER_INTERNAL_HAMT_COLLISION_H_

#include <concepts>
#include <functional>
#include <iterator>
#include <type_traits>

namespace mbo::container::container_internal {

template<
    std::forward_iterator Iterator,
    typename Hash,
    typename LookupKey,
    typename HashOf,
    typename KeyOf,
    typename Equal>
requires(
    std::is_nothrow_invocable_r_v<Hash, HashOf&, std::iter_reference_t<Iterator>>
    && std::is_nothrow_invocable_v<KeyOf&, std::iter_reference_t<Iterator>>
    && std::is_nothrow_invocable_r_v<
        bool,
        Equal&,
        std::invoke_result_t<KeyOf&, std::iter_reference_t<Iterator>>,
        const LookupKey&>)
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
