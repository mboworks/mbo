// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_REPLACE_H_
#define MBO_CONTAINER_INTERNAL_HAMT_REPLACE_H_

#include <concepts>
#include <cstddef>
#include <iterator>
#include <optional>
#include <type_traits>

#include "mbo/container/internal/hamt_hash_path.h"
#include "mbo/container/internal/hamt_lookup.h"
#include "mbo/container/internal/hamt_shared_node.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

template<typename Node>
struct HamtReplaceResult final {
  Node* root;
  bool replaced;
};

namespace hamt_replace_internal {

// The target has already been found in this root using this hash.
template<std::size_t FragmentBits, typename Entry, std::unsigned_integral Hash, mbo::memory::BlockSource Source>
[[nodiscard]] std::optional<HamtSharedNode<FragmentBits, Entry>*> TryReplaceAt(
    Source& source,
    const HamtSharedNode<FragmentBits, Entry>& original,
    const HamtHashPath<Hash, FragmentBits>& path,
    std::size_t level,
    const Entry* target,
    const Entry& replacement) noexcept
requires std::is_nothrow_copy_constructible_v<Entry> {
  using Node = HamtSharedNode<FragmentBits, Entry>;
  if (original.is_collision()) {
    const auto position = static_cast<std::size_t>(std::distance(original.entries().data(), target));
    return Node::TryReplaceEntry(source, original, position, replacement);
  }
  const std::size_t fragment = path.Fragment(level);
  if (original.index().Kind(fragment) == HamtSlotKind::kData) {
    return Node::TryReplaceEntry(source, original, original.index().DataIndex(fragment), replacement);
  }
  const std::size_t position = original.index().NodeIndex(fragment);
  const Node* const child = original.children().subspan(position).front();
  auto* const replaced = TryReplaceAt(source, *child, path, level + 1, target, replacement).value_or(nullptr);
  if (replaced == nullptr) {
    return std::nullopt;
  }
  const auto result = Node::TryReplaceChild(source, original, position, replaced);
  Node::Release(source, replaced);
  return result;
}

}  // namespace hamt_replace_internal

// Replacement must preserve the existing key and full hash. Borrows original;
// success owns one root reference, including a retained root for missing keys.
template<
    std::unsigned_integral Hash,
    std::size_t FragmentBits,
    typename Entry,
    typename Key,
    mbo::memory::BlockSource Source,
    typename HashOf,
    typename KeyOf,
    typename Equal>
requires(
    std::is_nothrow_copy_constructible_v<Entry> && std::is_nothrow_invocable_r_v<Hash, const HashOf&, const Entry&>
    && std::is_nothrow_invocable_v<const KeyOf&, const Entry&>
    && std::is_nothrow_invocable_r_v<bool, const Equal&, std::invoke_result_t<const KeyOf&, const Entry&>, const Key&>)
[[nodiscard]] std::optional<HamtReplaceResult<HamtSharedNode<FragmentBits, Entry>>> TryReplaceHamtEntry(
    Source& source,
    HamtSharedNode<FragmentBits, Entry>* original,
    Hash hash,
    const Key& key,
    const Entry& replacement,
    const HashOf& hash_of,
    const KeyOf& key_of,
    const Equal& equal) noexcept {
  using Node = HamtSharedNode<FragmentBits, Entry>;
  const Entry* const target = FindHamtEntry(original, hash, key, hash_of, key_of, equal);
  if (target == nullptr) {
    Node::Retain(original);
    return HamtReplaceResult<Node>{.root = original, .replaced = false};
  }
  const HamtHashPath<Hash, FragmentBits> path(hash);
  auto* const replaced =
      hamt_replace_internal::TryReplaceAt(source, *original, path, 0, target, replacement).value_or(nullptr);
  if (replaced == nullptr) {
    return std::nullopt;
  }
  return HamtReplaceResult<Node>{.root = replaced, .replaced = true};
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_REPLACE_H_
