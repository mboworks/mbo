// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_LOOKUP_H_
#define MBO_CONTAINER_INTERNAL_HAMT_LOOKUP_H_

#include <concepts>
#include <cstddef>
#include <functional>
#include <type_traits>

#include "mbo/container/internal/hamt_hash_path.h"
#include "mbo/container/internal/hamt_node_index.h"
#include "mbo/container/internal/hamt_shared_node.h"

namespace mbo::container::container_internal {

template<
    std::unsigned_integral Hash,
    std::size_t FragmentBits,
    typename Entry,
    typename Key,
    typename HashOf,
    typename KeyOf,
    typename Equal>
requires(
    std::is_nothrow_invocable_r_v<Hash, HashOf&, const Entry&> && std::is_nothrow_invocable_v<KeyOf&, const Entry&>
    && std::is_nothrow_invocable_r_v<bool, Equal&, std::invoke_result_t<KeyOf&, const Entry&>, const Key&>)
const Entry* FindHamtEntry(
    const HamtSharedNode<FragmentBits, Entry>* node,
    Hash hash,
    const Key& key,
    HashOf hash_of,
    KeyOf key_of,
    Equal equal) noexcept {
  const HamtHashPath<Hash, FragmentBits> path(hash);
  std::size_t level = 0;
  while (node != nullptr) {
    if (node->is_collision()) {
      for (const Entry& entry : node->entries()) {
        if (std::invoke(hash_of, entry) == hash && std::invoke(equal, std::invoke(key_of, entry), key)) {
          return std::addressof(entry);
        }
      }
      return nullptr;
    }
    if (level >= path.kLevels) {
      return nullptr;
    }
    const std::size_t fragment = path.fragment(level);
    switch (node->index().kind(fragment)) {
      case HamtSlotKind::kEmpty: return nullptr;
      case HamtSlotKind::kData: {
        const Entry& entry = node->entries()[node->index().data_index(fragment)];
        return std::invoke(hash_of, entry) == hash && std::invoke(equal, std::invoke(key_of, entry), key)
                   ? std::addressof(entry)
                   : nullptr;
      }
      case HamtSlotKind::kNode:
        node = node->children()[node->index().node_index(fragment)];
        ++level;
        break;
    }
  }
  return nullptr;
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_LOOKUP_H_
