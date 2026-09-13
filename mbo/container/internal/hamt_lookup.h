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
    std::is_nothrow_invocable_r_v<Hash, const HashOf&, const Entry&>
    && std::is_nothrow_invocable_v<const KeyOf&, const Entry&>
    && std::is_nothrow_invocable_r_v<bool, const Equal&, std::invoke_result_t<const KeyOf&, const Entry&>, const Key&>)
const Entry* FindHamtEntry(
    const HamtSharedNode<FragmentBits, Entry>* node,
    Hash hash,
    const Key& key,
    const HashOf& hash_of,
    const KeyOf& key_of,
    const Equal& equal) noexcept {
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
    const std::size_t fragment = path.Fragment(level);
    switch (node->index().Kind(fragment)) {
      case HamtSlotKind::kEmpty: return nullptr;
      case HamtSlotKind::kData: {
        const Entry& entry = node->entries()[node->index().DataIndex(fragment)];
        return std::invoke(hash_of, entry) == hash && std::invoke(equal, std::invoke(key_of, entry), key)
                   ? std::addressof(entry)
                   : nullptr;
      }
      case HamtSlotKind::kNode:
        node = node->children()[node->index().NodeIndex(fragment)];
        ++level;
        break;
    }
  }
  return nullptr;
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_LOOKUP_H_
