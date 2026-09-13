// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_ERASE_H_
#define MBO_CONTAINER_INTERNAL_HAMT_ERASE_H_

#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <type_traits>

#include "mbo/container/internal/hamt_hash_path.h"
#include "mbo/container/internal/hamt_shared_node.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

template<typename Node>
struct HamtEraseResult final {
  Node* root;
  bool erased;
};

namespace hamt_erase_internal {

template<typename Node, typename Entry>
struct HamtEraseStep final {
  Node* node = nullptr;
  std::optional<Entry> singleton;
  bool erased = false;
};

template<typename Node, typename Entry, mbo::memory::BlockSource Source>
std::optional<HamtEraseStep<Node, Entry>> Compact(Source& source, Node* node) noexcept
requires(std::is_nothrow_copy_constructible_v<Entry> && std::is_nothrow_move_constructible_v<Entry>)
{
  if (node != nullptr && !node->is_collision() && node->entries().empty() && node->children().empty()) {
    Node::Release(source, node);
    return HamtEraseStep<Node, Entry>{.erased = true};
  }
  if (node != nullptr && !node->is_collision() && node->entries().size() == 1 && node->children().empty()) {
    Entry singleton = node->entries().front();
    Node::Release(source, node);
    return HamtEraseStep<Node, Entry>{.singleton = singleton, .erased = true};
  }
  return HamtEraseStep<Node, Entry>{.node = node, .erased = true};
}

// Singleton propagation stores entries in optional intermediate results. Both
// copying and moving entries must therefore be nothrow, as must const callable
// invocation. Block-source exceptions escape into noexcept and terminate.
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
    std::is_nothrow_copy_constructible_v<Entry> && std::is_nothrow_move_constructible_v<Entry>
    && std::is_nothrow_invocable_r_v<Hash, const HashOf&, const Entry&>
    && std::is_nothrow_invocable_v<const KeyOf&, const Entry&>
    && std::is_nothrow_invocable_r_v<bool, const Equal&, std::invoke_result_t<const KeyOf&, const Entry&>, const Key&>)
std::optional<HamtEraseStep<HamtSharedNode<FragmentBits, Entry>, Entry>> TryEraseAt(
    Source& source,
    HamtSharedNode<FragmentBits, Entry>* original,
    Hash hash,
    const Key& key,
    std::size_t level,
    const HashOf& hash_of,
    const KeyOf& key_of,
    const Equal& equal) noexcept {
  using Node = HamtSharedNode<FragmentBits, Entry>;
  if (original->is_collision()) {
    std::size_t position = 0;
    for (const Entry& existing : original->entries()) {
      if (std::invoke(hash_of, existing) == hash && std::invoke(equal, std::invoke(key_of, existing), key)) {
        if (original->entries().size() == 1) {
          return HamtEraseStep<Node, Entry>{.erased = true};
        }
        if (original->entries().size() == 2) {
          return HamtEraseStep<Node, Entry>{.singleton = original->entries()[position == 0 ? 1 : 0], .erased = true};
        }
        auto erased = Node::TryEraseCollisionEntry(source, *original, position);
        if (!erased) {
          return std::nullopt;
        }
        return HamtEraseStep<Node, Entry>{.node = *erased, .erased = true};
      }
      ++position;
    }
    Node::Retain(original);
    return HamtEraseStep<Node, Entry>{.node = original, .erased = false};
  }

  const HamtHashPath<Hash, FragmentBits> path(hash);
  if (level >= path.kLevels) {
    Node::Retain(original);
    return HamtEraseStep<Node, Entry>{.node = original, .erased = false};
  }
  const std::size_t fragment = path.Fragment(level);
  auto index = original->index();
  switch (index.Kind(fragment)) {
    case HamtSlotKind::kEmpty:
      Node::Retain(original);
      return HamtEraseStep<Node, Entry>{.node = original, .erased = false};
    case HamtSlotKind::kData: {
      const std::size_t position = index.DataIndex(fragment);
      const Entry& existing = original->entries()[position];
      if (std::invoke(hash_of, existing) != hash || !std::invoke(equal, std::invoke(key_of, existing), key)) {
        Node::Retain(original);
        return HamtEraseStep<Node, Entry>{.node = original, .erased = false};
      }
      if (original->entries().size() == 1 && original->children().empty()) {
        return HamtEraseStep<Node, Entry>{.erased = true};
      }
      index.EraseData(fragment);
      auto erased = Node::TryEraseEntry(source, *original, index, position);
      if (!erased) {
        return std::nullopt;
      }
      return Compact<Node, Entry>(source, *erased);
    }
    case HamtSlotKind::kNode: {
      const std::size_t position = index.NodeIndex(fragment);
      auto erased = TryEraseAt<Hash, FragmentBits>(
          source, original->children()[position], hash, key, level + 1, hash_of, key_of, equal);
      if (!erased) {
        return std::nullopt;
      }
      if (!erased->erased) {
        Node::Release(source, erased->node);
        Node::Retain(original);
        return HamtEraseStep<Node, Entry>{.node = original, .erased = false};
      }
      std::optional<Node*> changed;
      if (erased->singleton) {
        index.DemoteNodeToData(fragment);
        changed = Node::TryDemoteChildToEntry(
            source, *original, index, position, index.DataIndex(fragment), *erased->singleton);
      } else if (erased->node == nullptr) {
        index.EraseNode(fragment);
        changed = Node::TryEraseChild(source, *original, index, position);
      } else {
        changed = Node::TryReplaceChild(source, *original, position, erased->node);
        Node::Release(source, erased->node);
      }
      if (!changed) {
        return std::nullopt;
      }
      return Compact<Node, Entry>(source, *changed);
    }
  }
  return std::nullopt;
}

}  // namespace hamt_erase_internal

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
    std::is_nothrow_copy_constructible_v<Entry> && std::is_nothrow_move_constructible_v<Entry>
    && std::is_nothrow_invocable_r_v<Hash, const HashOf&, const Entry&>
    && std::is_nothrow_invocable_v<const KeyOf&, const Entry&>
    && std::is_nothrow_invocable_r_v<bool, const Equal&, std::invoke_result_t<const KeyOf&, const Entry&>, const Key&>)
std::optional<HamtEraseResult<HamtSharedNode<FragmentBits, Entry>>> TryEraseHamtEntry(
    Source& source,
    HamtSharedNode<FragmentBits, Entry>* root,
    Hash hash,
    const Key& key,
    const HashOf& hash_of,
    const KeyOf& key_of,
    const Equal& equal) noexcept {
  using Node = HamtSharedNode<FragmentBits, Entry>;
  if (root == nullptr) {
    return HamtEraseResult<Node>{.root = nullptr, .erased = false};
  }
  auto erased = hamt_erase_internal::TryEraseAt<Hash, FragmentBits>(source, root, hash, key, 0, hash_of, key_of, equal);
  if (!erased) {
    return std::nullopt;
  }
  if (!erased->singleton) {
    return HamtEraseResult<Node>{.root = erased->node, .erased = erased->erased};
  }
  typename Node::index_type index;
  const Hash remaining_hash = std::invoke(hash_of, *erased->singleton);
  index.InsertData(HamtHashPath<Hash, FragmentBits>(remaining_hash).Fragment(0));
  const auto entries = std::to_array<Entry>({*erased->singleton});
  auto singleton = Node::TryCreate(source, index, entries, std::span<Node* const>{});
  if (!singleton) {
    return std::nullopt;
  }
  return HamtEraseResult<Node>{.root = *singleton, .erased = true};
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_ERASE_H_
