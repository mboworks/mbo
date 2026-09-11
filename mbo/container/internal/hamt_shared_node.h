// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_SHARED_NODE_H_
#define MBO_CONTAINER_INTERNAL_HAMT_SHARED_NODE_H_

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>

#include "mbo/container/internal/hamt_node_index.h"
#include "mbo/container/internal/hamt_packed_node_layout.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

template<std::size_t FragmentBits, typename Entry>
class HamtSharedNode final {
 public:
  using node_type = HamtSharedNode;
  using index_type = HamtNodeIndex<FragmentBits>;

  explicit HamtSharedNode(index_type index) noexcept : index_(index) {}

  HamtSharedNode(const HamtSharedNode&) = delete;
  HamtSharedNode& operator=(const HamtSharedNode&) = delete;

  std::span<Entry> entries() noexcept { return {EntryPtr(), index_.data_size()}; }

  std::span<const Entry> entries() const noexcept { return {EntryPtr(), index_.data_size()}; }

  std::span<node_type*> children() noexcept { return {ChildPtr(), index_.node_size()}; }

  std::span<node_type* const> children() const noexcept { return {ChildPtr(), index_.node_size()}; }

  const index_type& index() const noexcept { return index_; }

  std::uint32_t use_count() const noexcept { return references_.load(std::memory_order_relaxed); }

  static void Retain(node_type* node) noexcept {
    if (node != nullptr) {
      node->references_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryCreate(
      Source& source,
      index_type index,
      std::span<const Entry> entries,
      std::span<node_type* const> children) noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    if (entries.size() != index.data_size() || children.size() != index.node_size()) {
      return std::nullopt;
    }
    const auto layout = Layout::TryMake(entries.size(), children.size());
    if (!layout) {
      return std::nullopt;
    }
    const auto block = source.TryAcquire(layout->size, layout->alignment);
    if (!Usable(block, *layout)) {
      if (block) {
        source.Release(*block);
      }
      return std::nullopt;
    }
    auto* const node = std::construct_at(
        reinterpret_cast<node_type*>(block->data),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        index);
    std::uninitialized_copy(entries.begin(), entries.end(), node->EntryPtr());
    std::uninitialized_copy(children.begin(), children.end(), node->ChildPtr());
    for (node_type* child : children) {
      Retain(child);
    }
    return node;
  }

  template<mbo::memory::BlockSource Source>
  static void Release(Source& source, node_type* node) noexcept {
    if (node == nullptr || node->references_.fetch_sub(1, std::memory_order_acq_rel) != 1) {
      return;
    }
    const index_type index = node->index_;
    const auto layout = *Layout::TryMake(index.data_size(), index.node_size());
    for (node_type* child : node->children()) {
      Release(source, child);
    }
    std::destroy_n(node->EntryPtr(), index.data_size());
    std::destroy_n(node->ChildPtr(), index.node_size());
    std::destroy_at(node);
    source.Release({
        .data = reinterpret_cast<std::byte*>(node),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        .size = layout.size,
        .alignment = layout.alignment,
    });
  }

 private:
  using Layout = HamtPackedNodeLayout<node_type, Entry, node_type*>;

  static bool Usable(const std::optional<mbo::memory::MemoryBlock>& block, const Layout& layout) noexcept {
    return block && block->data != nullptr && block->size >= layout.size && block->alignment >= layout.alignment
           && std::bit_cast<std::uintptr_t>(block->data) % layout.alignment == 0;
  }

  Entry* EntryPtr() const noexcept {
    const auto layout = *Layout::TryMake(index_.data_size(), index_.node_size());
    auto* const bytes = reinterpret_cast<std::byte*>(const_cast<node_type*>(this));  // NOLINT
    return reinterpret_cast<Entry*>(bytes + layout.data_offset);                     // NOLINT
  }

  node_type** ChildPtr() const noexcept {
    const auto layout = *Layout::TryMake(index_.data_size(), index_.node_size());
    auto* const bytes = reinterpret_cast<std::byte*>(const_cast<node_type*>(this));  // NOLINT
    return reinterpret_cast<node_type**>(bytes + layout.child_offset);               // NOLINT
  }

  std::atomic<std::uint32_t> references_{1};
  index_type index_;
};

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_SHARED_NODE_H_
