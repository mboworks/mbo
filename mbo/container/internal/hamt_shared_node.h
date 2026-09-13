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

// Intrusive ownership of one packed node allocation. Every Release must use the
// same block source that created the node and its descendants. Child references
// are retained on creation and released with the last parent reference. Atomic
// reference counting does not make entry or child mutation thread-safe; callers
// must externally synchronize access and retain a live reference before use.
// Entry copying and destruction must be nothrow. Allocation failure is reported as nullopt;
// exceptions escaping a block source terminate through this noexcept interface.
// NOLINTBEGIN(readability-identifier-naming) -- Container vocabulary follows STL spelling.
template<std::size_t FragmentBits, typename Entry>
class HamtSharedNode final {
  static_assert(std::is_nothrow_destructible_v<Entry>, "Packed HAMT entries must have nothrow destruction");

 public:
  using node_type = HamtSharedNode;
  using index_type = HamtNodeIndex<FragmentBits>;

  explicit HamtSharedNode(index_type index, std::size_t collision_count = 0) noexcept
      : index_(index), collision_count_(collision_count) {}

  HamtSharedNode(const HamtSharedNode&) = delete;
  HamtSharedNode& operator=(const HamtSharedNode&) = delete;
  HamtSharedNode(HamtSharedNode&&) = delete;
  HamtSharedNode& operator=(HamtSharedNode&&) = delete;
  ~HamtSharedNode() = default;

  std::span<Entry> entries() noexcept { return {EntryPtr(), EntryCount()}; }

  std::span<const Entry> entries() const noexcept { return {EntryPtr(), EntryCount()}; }

  std::span<node_type*> children() noexcept { return {ChildPtr(), index_.NodeSize()}; }

  std::span<node_type* const> children() const noexcept { return {ChildPtr(), index_.NodeSize()}; }

  const index_type& index() const noexcept { return index_; }

  bool is_collision() const noexcept { return collision_count_ != 0; }

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
    if (entries.size() != index.DataSize() || children.size() != index.NodeSize()) {
      return std::nullopt;
    }
    return TryAllocate(source, index, entries, children, 0);
  }

  // All entries must have the same full hash, established by the caller. Empty
  // collision nodes are rejected; a collision node has no indexed children.
  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryCreateCollision(Source& source, std::span<const Entry> entries) noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    if (entries.empty()) {
      return std::nullopt;
    }
    return TryAllocate(source, {}, entries, {}, entries.size());
  }

  // index describes the resulting occupancy; position is the dense entry rank
  // of the newly occupied slot. The original node remains untouched.
  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryInsertEntry(
      Source& source,
      const node_type& original,
      index_type index,
      std::size_t position,
      const Entry& entry) noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    if (original.is_collision() || index.DataSize() != original.entries().size() + 1
        || index.NodeSize() != original.children().size() || position >= index.DataSize()) {
      return std::nullopt;
    }
    const auto result = TryAllocateUninitialized(source, index, index.DataSize(), original.children(), 0);
    if (!result) {
      return std::nullopt;
    }
    node_type* const node = *result;
    const auto entries = original.entries();
    std::uninitialized_copy_n(entries.begin(), position, node->EntryPtr());
    std::construct_at(node->EntryPtr() + position, entry);
    std::uninitialized_copy(
        entries.begin() + static_cast<std::ptrdiff_t>(position), entries.end(), node->EntryPtr() + position + 1);
    return node;
  }

  template<mbo::memory::BlockSource Source>
  static void Release(Source& source, node_type* node) noexcept {
    if (node == nullptr || node->references_.fetch_sub(1, std::memory_order_acq_rel) != 1) {
      return;
    }
    const std::size_t entry_count = node->EntryCount();
    const index_type index = node->index_;
    const auto block = node->block_;
    for (node_type* child : node->children()) {
      Release(source, child);
    }
    std::destroy_n(node->EntryPtr(), entry_count);
    std::destroy_n(node->ChildPtr(), index.NodeSize());
    std::destroy_at(node);
    source.Release(block);
  }

 private:
  using Layout = HamtPackedNodeLayout<node_type, Entry, node_type*>;

  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryAllocate(
      Source& source,
      index_type index,
      std::span<const Entry> entries,
      std::span<node_type* const> children,
      std::size_t collision_count) noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    const auto result = TryAllocateUninitialized(source, index, entries.size(), children, collision_count);
    if (result) {
      std::uninitialized_copy(entries.begin(), entries.end(), (*result)->EntryPtr());
    }
    return result;
  }

  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryAllocateUninitialized(
      Source& source,
      index_type index,
      std::size_t entry_count,
      std::span<node_type* const> children,
      std::size_t collision_count) noexcept {
    const auto layout = Layout::TryMake(entry_count, children.size());
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
        index, collision_count);
    node->block_ = *block;
    std::uninitialized_copy(children.begin(), children.end(), node->ChildPtr());
    for (node_type* child : children) {
      Retain(child);
    }
    return node;
  }

  static bool Usable(const std::optional<mbo::memory::MemoryBlock>& block, const Layout& layout) noexcept {
    return block && block->data != nullptr && block->size >= layout.size && block->alignment >= layout.alignment
           && std::bit_cast<std::uintptr_t>(block->data) % layout.alignment == 0;
  }

  Entry* EntryPtr() const noexcept {
    const auto layout = *Layout::TryMake(EntryCount(), index_.NodeSize());
    auto* const bytes = reinterpret_cast<std::byte*>(const_cast<node_type*>(this));  // NOLINT
    return reinterpret_cast<Entry*>(bytes + layout.data_offset);                     // NOLINT
  }

  node_type** ChildPtr() const noexcept {
    const auto layout = *Layout::TryMake(EntryCount(), index_.NodeSize());
    auto* const bytes = reinterpret_cast<std::byte*>(const_cast<node_type*>(this));  // NOLINT
    return reinterpret_cast<node_type**>(bytes + layout.child_offset);               // NOLINT
  }

  std::size_t EntryCount() const noexcept { return is_collision() ? collision_count_ : index_.DataSize(); }

  std::atomic<std::uint32_t> references_{1};
  index_type index_;
  mbo::memory::MemoryBlock block_;
  std::size_t collision_count_ = 0;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_SHARED_NODE_H_
