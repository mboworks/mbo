// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_CLONE_H_
#define MBO_CONTAINER_INTERNAL_HAMT_CLONE_H_

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>

#include "mbo/container/internal/hamt_shared_node.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

// Deep-copies a borrowed valid tree into destination without retaining any source
// node. The result owns one reference; release it through destination only.
// Null input is a successful empty tree. Failure reclaims all partial copies.
template<std::size_t FragmentBits, typename Entry, mbo::memory::BlockSource Source>
std::optional<HamtSharedNode<FragmentBits, Entry>*> TryCloneHamtTree(
    Source& destination,
    const HamtSharedNode<FragmentBits, Entry>* original) noexcept
requires std::is_nothrow_copy_constructible_v<Entry>
{
  using Node = HamtSharedNode<FragmentBits, Entry>;
  if (original == nullptr) {
    return static_cast<Node*>(nullptr);
  }
  if (original->is_collision()) {
    return Node::TryCreateCollision(destination, original->entries());
  }
  std::array<Node*, Node::index_type::kSlotCount> children{};
  std::size_t count = 0;
  for (const Node* child : original->children()) {
    const auto cloned = TryCloneHamtTree(destination, child);
    if (!cloned) {
      for (Node* acquired : std::span(children).first(count)) {
        Node::Release(destination, acquired);
      }
      return std::nullopt;
    }
    children.at(count++) = *cloned;
  }
  const auto copied_children = std::span(children).first(count);
  const auto result = Node::TryCreate(destination, original->index(), original->entries(), copied_children);
  for (Node* child : copied_children) {
    Node::Release(destination, child);
  }
  return result;
}

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_CLONE_H_
