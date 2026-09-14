// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_UPDATE_H_
#define MBO_CONTAINER_INTERNAL_HAMT_UPDATE_H_

#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>

#include "mbo/container/internal/hamt_hash_path.h"
#include "mbo/container/internal/hamt_lookup.h"
#include "mbo/container/internal/hamt_replace.h"
#include "mbo/container/internal/hamt_shared_node.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

namespace hamt_update_internal {

// target is an entry already found through this hash path. Every ancestor must
// be unique: a unique leaf below a shared ancestor is still snapshot-shared.
template<std::size_t FragmentBits, typename Entry, std::unsigned_integral Hash>
Entry* FindUniqueEntry(HamtSharedNode<FragmentBits, Entry>* node, Hash hash, const Entry* target) noexcept {
  const HamtHashPath<Hash, FragmentBits> path(hash);
  std::size_t level = 0;
  for (;;) {
    if (node->use_count() != 1) {
      return nullptr;
    }
    if (node->is_collision()) {
      const Entry* const beginning = node->entries().data();
      const auto position = static_cast<std::size_t>(std::distance(beginning, target));
      return std::addressof(node->entries().subspan(position).front());
    }
    const std::size_t fragment = path.Fragment(level++);
    if (node->index().Kind(fragment) == HamtSlotKind::kData) {
      return std::addressof(node->entries().subspan(node->index().DataIndex(fragment)).front());
    }
    node = node->children().subspan(node->index().NodeIndex(fragment)).front();
  }
}

}  // namespace hamt_update_internal

// Transient-only primitive. Editor preserves key/hash and cannot reenter tree
// operations. Unique paths edit in place; shared paths copy before publication.
// Failure preserves the container, not external side effects of the editor.
template<
    std::unsigned_integral Hash,
    std::size_t FragmentBits,
    typename Entry,
    typename Key,
    mbo::memory::BlockSource Source,
    typename HashOf,
    typename KeyOf,
    typename Equal,
    typename Editor>
requires(
    std::is_nothrow_copy_constructible_v<Entry> && std::is_nothrow_invocable_r_v<Hash, const HashOf&, const Entry&>
    && std::is_nothrow_invocable_v<const KeyOf&, const Entry&>
    && std::is_nothrow_invocable_r_v<bool, const Equal&, std::invoke_result_t<const KeyOf&, const Entry&>, const Key&>
    && std::is_nothrow_invocable_v<const Editor&, Entry&>
    && std::same_as<std::invoke_result_t<const Editor&, Entry&>, void>)
[[nodiscard]] std::optional<HamtReplaceResult<HamtSharedNode<FragmentBits, Entry>>> TryUpdateHamtEntry(
    Source& source,
    HamtSharedNode<FragmentBits, Entry>* original,
    Hash hash,
    const Key& key,
    const HashOf& hash_of,
    const KeyOf& key_of,
    const Equal& equal,
    const Editor& editor) noexcept {
  using Node = HamtSharedNode<FragmentBits, Entry>;
  const Entry* const target = FindHamtEntry(original, hash, key, hash_of, key_of, equal);
  if (target == nullptr) {
    Node::Retain(original);
    return HamtReplaceResult<Node>{.root = original, .replaced = false};
  }
  Entry* const unique = hamt_update_internal::FindUniqueEntry(original, hash, target);
  if (unique != nullptr) {
    std::invoke(editor, *unique);
    Node::Retain(original);
    return HamtReplaceResult<Node>{.root = original, .replaced = true};
  }
  Entry replacement = *target;
  std::invoke(editor, replacement);
  return TryReplaceHamtEntry(source, original, hash, key, replacement, hash_of, key_of, equal);
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_UPDATE_H_
