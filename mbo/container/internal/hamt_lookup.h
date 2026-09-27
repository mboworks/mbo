// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_LOOKUP_H_
#define MBO_CONTAINER_INTERNAL_HAMT_LOOKUP_H_

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>

#include "mbo/container/internal/hamt_hash_path.h"
#include "mbo/container/internal/hamt_node_index.h"
#include "mbo/container/internal/hamt_shared_node.h"

namespace mbo::container::container_internal {

template<typename Entry>
struct HamtLookupResult final {
  const Entry* entry = nullptr;
  std::size_t full_hash_entries = 0;
};

// Borrows a valid immutable tree and returns a borrowed entry or nullptr.
// Bitmap routing follows one hash path; terminal collision buckets are scanned.
// Full hashes are checked before invoking key extraction or equality. Callable
// state is never copied and const invocation must be non-throwing.

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
HamtLookupResult<Entry> InspectHamtEntry(
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
      const auto entries = node->entries();
      if (entries.empty() || std::invoke(hash_of, entries.front()) != hash) {
        return {};
      }
      for (const Entry& entry : entries) {
        if (std::invoke(equal, std::invoke(key_of, entry), key)) {
          return {.entry = std::addressof(entry), .full_hash_entries = entries.size()};
        }
      }
      return {.full_hash_entries = entries.size()};
    }
    if (level >= path.kLevels) {
      return {};
    }
    const std::size_t fragment = path.Fragment(level);
    switch (node->index().Kind(fragment)) {
      case HamtSlotKind::kEmpty: return {};
      case HamtSlotKind::kData: {
        const Entry& entry = node->entries().subspan(node->index().DataIndex(fragment)).front();
        if (std::invoke(hash_of, entry) != hash) {
          return {};
        }
        return {
            .entry = std::invoke(equal, std::invoke(key_of, entry), key) ? std::addressof(entry) : nullptr,
            .full_hash_entries = 1};
      }
      case HamtSlotKind::kNode:
        node = node->children().subspan(node->index().NodeIndex(fragment)).front();
        ++level;
        break;
    }
  }
  return {};
}

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
  return InspectHamtEntry(node, hash, key, hash_of, key_of, equal).entry;
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_LOOKUP_H_
