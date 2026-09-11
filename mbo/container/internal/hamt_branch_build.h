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
  const auto merge = FindHamtMergePath<Hash, FragmentBits>(existing_hash, inserted_hash, start_level);
  if (merge.full_hash_collision) {
    const std::array entries = {existing, inserted};
    return Node::TryCreateCollision(source, entries);
  }

  typename Node::index_type divergence_index;
  divergence_index.insert_data(merge.existing_fragment);
  divergence_index.insert_data(merge.inserted_fragment);
  const std::array entries = merge.existing_fragment < merge.inserted_fragment ? std::array{existing, inserted}
                                                                               : std::array{inserted, existing};
  auto current = Node::TryCreate(source, divergence_index, entries, std::span<Node* const>{});
  if (!current) {
    return std::nullopt;
  }

  const HamtHashPath<Hash, FragmentBits> path(existing_hash);
  for (std::size_t offset = merge.common_levels; offset > 0; --offset) {
    typename Node::index_type parent_index;
    parent_index.insert_node(path.fragment(start_level + offset - 1));
    const std::array<Node*, 1> child = {*current};
    auto parent = Node::TryCreate(source, parent_index, std::span<const Entry>{}, child);
    Node::Release(source, *current);
    if (!parent) {
      return std::nullopt;
    }
    current = parent;
  }
  return current;
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_BRANCH_BUILD_H_
