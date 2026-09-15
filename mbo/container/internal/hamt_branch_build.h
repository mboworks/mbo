// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_BRANCH_BUILD_H_
#define MBO_CONTAINER_INTERNAL_HAMT_BRANCH_BUILD_H_

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>

#include "mbo/container/internal/hamt_hash_path.h"
#include "mbo/container/internal/hamt_merge_path.h"
#include "mbo/container/internal/hamt_shared_node.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

namespace hamt_branch_build_internal {

// Takes ownership of current, including on failure.
template<std::size_t FragmentBits, typename Entry, std::unsigned_integral Hash, mbo::memory::BlockSource Source>
std::optional<HamtSharedNode<FragmentBits, Entry>*> TryWrapPrefix(
    Source& source,
    HamtSharedNode<FragmentBits, Entry>* current,
    Hash hash,
    std::size_t start_level,
    std::size_t common_levels) noexcept
requires std::is_nothrow_copy_constructible_v<Entry>
{
  using Node = HamtSharedNode<FragmentBits, Entry>;
  const HamtHashPath<Hash, FragmentBits> path(hash);
  for (std::size_t offset = common_levels; offset > 0; --offset) {
    typename Node::index_type index;
    index.InsertNode(path.Fragment(start_level + offset - 1));
    const auto children = std::to_array<Node*>({current});
    const auto parent = Node::TryCreate(source, index, std::span<const Entry>{}, children);
    Node::Release(source, current);
    if (!parent) {
      return std::nullopt;
    }
    current = *parent;
  }
  return current;
}

}  // namespace hamt_branch_build_internal

// The fragments before start_level must already match. Returns one owning
// reference, or nullopt after releasing every partially constructed node.
template<std::size_t FragmentBits, typename Entry, std::unsigned_integral Hash, mbo::memory::BlockSource Source>
std::optional<HamtSharedNode<FragmentBits, Entry>*> TryBuildHamtBranch(
    Source& source,
    Hash existing_hash,
    const Entry& existing,
    Hash inserted_hash,
    const Entry& inserted,
    std::size_t start_level) noexcept
requires std::is_nothrow_copy_constructible_v<Entry>
{
  using Node = HamtSharedNode<FragmentBits, Entry>;
  if (start_level > HamtHashPath<Hash, FragmentBits>::kLevels
      || (start_level == HamtHashPath<Hash, FragmentBits>::kLevels && existing_hash != inserted_hash)) {
    return std::nullopt;
  }
  const auto merge = FindHamtMergePath<Hash, FragmentBits>(existing_hash, inserted_hash, start_level);
  if (merge.full_hash_collision) {
    const auto entries = std::to_array<Entry>({existing, inserted});
    return Node::TryCreateCollision(source, entries);
  }

  typename Node::index_type divergence_index;
  divergence_index.InsertData(merge.existing_fragment);
  divergence_index.InsertData(merge.inserted_fragment);
  const auto entries = merge.existing_fragment < merge.inserted_fragment ? std::to_array<Entry>({existing, inserted})
                                                                         : std::to_array<Entry>({inserted, existing});
  auto current = Node::TryCreate(source, divergence_index, entries, std::span<Node* const>{});
  if (!current) {
    return std::nullopt;
  }

  return hamt_branch_build_internal::TryWrapPrefix<FragmentBits>(
      source, *current, existing_hash, start_level, merge.common_levels);
}

// Split a borrowed full-hash collision node from an entry with a different
// full hash. The result owns a retained reference to existing; failure leaves
// its ownership unchanged. Fragments before start_level must already match.
template<std::size_t FragmentBits, typename Entry, std::unsigned_integral Hash, mbo::memory::BlockSource Source>
std::optional<HamtSharedNode<FragmentBits, Entry>*> TryBuildHamtCollisionBranch(
    Source& source,
    HamtSharedNode<FragmentBits, Entry>* existing,
    Hash existing_hash,
    Hash inserted_hash,
    const Entry& inserted,
    std::size_t start_level) noexcept
requires std::is_nothrow_copy_constructible_v<Entry>
{
  using Node = HamtSharedNode<FragmentBits, Entry>;
  if (existing == nullptr || !existing->is_collision() || existing_hash == inserted_hash
      || start_level >= HamtHashPath<Hash, FragmentBits>::kLevels) {
    return std::nullopt;
  }
  const auto merge = FindHamtMergePath<Hash, FragmentBits>(existing_hash, inserted_hash, start_level);
  typename Node::index_type index;
  index.InsertNode(merge.existing_fragment);
  index.InsertData(merge.inserted_fragment);
  const auto entries = std::to_array<Entry>({inserted});
  const auto children = std::to_array<Node*>({existing});
  const auto current = Node::TryCreate(source, index, entries, children);
  if (!current) {
    return std::nullopt;
  }
  return hamt_branch_build_internal::TryWrapPrefix<FragmentBits>(
      source, *current, existing_hash, start_level, merge.common_levels);
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_BRANCH_BUILD_H_
