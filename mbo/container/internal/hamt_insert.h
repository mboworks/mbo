// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_INSERT_H_
#define MBO_CONTAINER_INTERNAL_HAMT_INSERT_H_

#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <type_traits>

#include "mbo/container/internal/hamt_branch_build.h"
#include "mbo/container/internal/hamt_hash_path.h"
#include "mbo/container/internal/hamt_shared_node.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

template<typename Node>
struct HamtInsertResult final {
  Node* root;
  bool inserted;
};

namespace hamt_insert_internal {

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
    std::is_nothrow_copy_constructible_v<Entry> && std::is_nothrow_invocable_r_v<Hash, HashOf&, const Entry&>
    && std::is_nothrow_invocable_v<KeyOf&, const Entry&>
    && std::is_nothrow_invocable_r_v<bool, Equal&, std::invoke_result_t<KeyOf&, const Entry&>, const Key&>)
std::optional<HamtInsertResult<HamtSharedNode<FragmentBits, Entry>>> TryInsertAt(
    Source& source,
    HamtSharedNode<FragmentBits, Entry>* original,
    Hash hash,
    const Key& key,
    const Entry& entry,
    std::size_t level,
    HashOf& hash_of,
    KeyOf& key_of,
    Equal& equal) noexcept {
  using Node = HamtSharedNode<FragmentBits, Entry>;
  if (original->is_collision()) {
    std::size_t position = 0;
    for (const Entry& existing : original->entries()) {
      if (std::invoke(hash_of, existing) == hash && std::invoke(equal, std::invoke(key_of, existing), key)) {
        Node::Retain(original);
        return HamtInsertResult<Node>{.root = original, .inserted = false};
      }
      ++position;
    }
    auto inserted = Node::TryInsertCollisionEntry(source, *original, position, entry);
    if (!inserted) {
      return std::nullopt;
    }
    return HamtInsertResult<Node>{.root = *inserted, .inserted = true};
  }

  const HamtHashPath<Hash, FragmentBits> path(hash);
  if (level >= path.kLevels) {
    return std::nullopt;
  }
  const std::size_t fragment = path.fragment(level);
  auto index = original->index();
  switch (index.kind(fragment)) {
    case HamtSlotKind::kEmpty: {
      index.insert_data(fragment);
      auto inserted = Node::TryInsertEntry(source, *original, index, index.data_index(fragment), entry);
      if (!inserted) {
        return std::nullopt;
      }
      return HamtInsertResult<Node>{.root = *inserted, .inserted = true};
    }
    case HamtSlotKind::kData: {
      const std::size_t position = index.data_index(fragment);
      const Entry& existing = original->entries()[position];
      const Hash existing_hash = std::invoke(hash_of, existing);
      if (existing_hash == hash && std::invoke(equal, std::invoke(key_of, existing), key)) {
        Node::Retain(original);
        return HamtInsertResult<Node>{.root = original, .inserted = false};
      }
      auto branch = TryBuildHamtBranch<FragmentBits>(source, existing_hash, existing, hash, entry, level + 1);
      if (!branch) {
        return std::nullopt;
      }
      index.promote_data_to_node(fragment);
      auto promoted =
          Node::TryPromoteEntryToChild(source, *original, index, position, index.node_index(fragment), *branch);
      Node::Release(source, *branch);
      if (!promoted) {
        return std::nullopt;
      }
      return HamtInsertResult<Node>{.root = *promoted, .inserted = true};
    }
    case HamtSlotKind::kNode: {
      const std::size_t position = index.node_index(fragment);
      auto inserted = TryInsertAt<Hash, FragmentBits>(
          source, original->children()[position], hash, key, entry, level + 1, hash_of, key_of, equal);
      if (!inserted) {
        return std::nullopt;
      }
      if (!inserted->inserted) {
        Node::Release(source, inserted->root);
        Node::Retain(original);
        return HamtInsertResult<Node>{.root = original, .inserted = false};
      }
      auto replaced = Node::TryReplaceChild(source, *original, position, inserted->root);
      Node::Release(source, inserted->root);
      if (!replaced) {
        return std::nullopt;
      }
      return HamtInsertResult<Node>{.root = *replaced, .inserted = true};
    }
  }
  return std::nullopt;
}

}  // namespace hamt_insert_internal

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
    std::is_nothrow_copy_constructible_v<Entry> && std::is_nothrow_invocable_r_v<Hash, HashOf&, const Entry&>
    && std::is_nothrow_invocable_v<KeyOf&, const Entry&>
    && std::is_nothrow_invocable_r_v<bool, Equal&, std::invoke_result_t<KeyOf&, const Entry&>, const Key&>)
std::optional<HamtInsertResult<HamtSharedNode<FragmentBits, Entry>>> TryInsertHamtEntry(
    Source& source,
    HamtSharedNode<FragmentBits, Entry>* root,
    Hash hash,
    const Key& key,
    const Entry& entry,
    HashOf hash_of,
    KeyOf key_of,
    Equal equal) noexcept {
  using Node = HamtSharedNode<FragmentBits, Entry>;
  if (root == nullptr) {
    typename Node::index_type index;
    const HamtHashPath<Hash, FragmentBits> path(hash);
    index.insert_data(path.fragment(0));
    const std::array entries = {entry};
    auto inserted = Node::TryCreate(source, index, entries, std::span<Node* const>{});
    if (!inserted) {
      return std::nullopt;
    }
    return HamtInsertResult<Node>{.root = *inserted, .inserted = true};
  }
  return hamt_insert_internal::TryInsertAt<Hash, FragmentBits>(
      source, root, hash, key, entry, 0, hash_of, key_of, equal);
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_INSERT_H_
