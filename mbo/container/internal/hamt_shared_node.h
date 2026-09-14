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
  static_assert(std::is_nothrow_destructible_v<Entry>, "Shared HAMT entries require non-throwing destruction");

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
  // of the newly occupied slot. The caller preserves all existing data/child
  // slots and inserts exactly one data slot. The original remains untouched;
  // entry may alias one of its entries. This layer does not deduplicate keys.
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
    const auto result = TryAllocateUninitialized(source, index, index.DataSize(), index.NodeSize(), 0);
    if (!result) {
      return std::nullopt;
    }
    const node_type* const node = *result;
    CopyInsertedEntries(*node, original.entries(), position, entry);
    CopyChildren(*node, original.children());
    return result;
  }

  // index describes the resulting occupancy; position is the dense child rank
  // of the new node slot. The new node owns an additional reference to child.
  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryInsertChild(
      Source& source,
      const node_type& original,
      index_type index,
      std::size_t position,
      node_type* child) noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    if (child == nullptr || original.is_collision() || index.DataSize() != original.entries().size()
        || index.NodeSize() != original.children().size() + 1 || position >= index.NodeSize()) {
      return std::nullopt;
    }
    const auto result = TryAllocateUninitialized(source, index, index.DataSize(), index.NodeSize(), 0);
    if (!result) {
      return std::nullopt;
    }
    const node_type* const node = *result;
    std::uninitialized_copy(original.entries().begin(), original.entries().end(), node->EntryPtr());
    const auto children = original.children();
    std::uninitialized_copy_n(children.begin(), position, node->ChildPtr());
    std::construct_at(node->ChildPtr() + position, child);
    std::uninitialized_copy(
        children.begin() + static_cast<std::ptrdiff_t>(position), children.end(), node->ChildPtr() + position + 1);
    RetainChildren(node->children());
    return result;
  }

  // Path-copy a child slot without changing either the original node or its
  // ownership. Replacing a child with itself still creates a separately owned
  // node and retains the shared child exactly once for that new owner.
  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryReplaceChild(
      Source& source,
      const node_type& original,
      std::size_t position,
      node_type* replacement) noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    if (replacement == nullptr || original.is_collision() || position >= original.children().size()) {
      return std::nullopt;
    }
    const auto result =
        TryAllocateUninitialized(source, original.index_, original.entries().size(), original.children().size(), 0);
    if (!result) {
      return std::nullopt;
    }
    const node_type* const node = *result;
    std::uninitialized_copy(original.entries().begin(), original.entries().end(), node->EntryPtr());
    const auto children = original.children();
    std::uninitialized_copy_n(children.begin(), position, node->ChildPtr());
    std::construct_at(node->ChildPtr() + position, replacement);
    std::uninitialized_copy(
        children.begin() + static_cast<std::ptrdiff_t>(position + 1), children.end(), node->ChildPtr() + position + 1);
    RetainChildren(node->children());
    return result;
  }

  // index describes occupancy after removal. Path copying preserves the
  // original entries and retains all children for the new owner.
  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryPromoteEntryToChild(
      Source& source,
      const node_type& original,
      index_type index,
      std::size_t entry_position,
      std::size_t child_position,
      node_type* child) noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    if (child == nullptr || original.is_collision() || original.entries().empty()
        || index.DataSize() + 1 != original.entries().size() || index.NodeSize() != original.children().size() + 1
        || entry_position >= original.entries().size() || child_position >= index.NodeSize()) {
      return std::nullopt;
    }
    const auto result = TryAllocateUninitialized(source, index, index.DataSize(), index.NodeSize(), 0);
    if (!result) {
      return std::nullopt;
    }
    const node_type* const node = *result;
    CopyErasedEntries(*node, original.entries(), entry_position);
    const auto children = original.children();
    std::uninitialized_copy_n(children.begin(), child_position, node->ChildPtr());
    std::construct_at(node->ChildPtr() + child_position, child);
    std::uninitialized_copy(
        children.begin() + static_cast<std::ptrdiff_t>(child_position), children.end(),
        node->ChildPtr() + child_position + 1);
    RetainChildren(node->children());
    return result;
  }

  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryDemoteChildToEntry(
      Source& source,
      const node_type& original,
      index_type index,
      std::size_t child_position,
      std::size_t entry_position,
      const Entry& entry) noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    if (original.is_collision() || original.children().empty() || index.DataSize() != original.entries().size() + 1
        || index.NodeSize() + 1 != original.children().size() || child_position >= original.children().size()
        || entry_position >= index.DataSize()) {
      return std::nullopt;
    }
    const auto result = TryAllocateUninitialized(source, index, index.DataSize(), index.NodeSize(), 0);
    if (!result) {
      return std::nullopt;
    }
    node_type* const node = *result;
    CopyInsertedEntries(*node, original.entries(), entry_position, entry);
    const auto children = original.children();
    std::uninitialized_copy_n(children.begin(), child_position, node->ChildPtr());
    std::uninitialized_copy(
        children.begin() + static_cast<std::ptrdiff_t>(child_position + 1), children.end(),
        node->ChildPtr() + child_position);
    RetainChildren(node->children());
    return node;
  }

  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryEraseEntry(
      Source& source,
      const node_type& original,
      index_type index,
      std::size_t position) noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    if (original.is_collision() || original.entries().empty() || index.DataSize() + 1 != original.entries().size()
        || index.NodeSize() != original.children().size() || position >= original.entries().size()) {
      return std::nullopt;
    }
    const auto result = TryAllocateUninitialized(source, index, index.DataSize(), index.NodeSize(), 0);
    if (!result) {
      return std::nullopt;
    }
    const node_type* const node = *result;
    CopyErasedEntries(*node, original.entries(), position);
    CopyChildren(*node, original.children());
    return result;
  }

  // The copied node owns only the remaining children. The removed child stays
  // owned by the original node until that original owner's last release.
  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryEraseChild(
      Source& source,
      const node_type& original,
      index_type index,
      std::size_t position) noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    if (original.is_collision() || original.children().empty() || index.DataSize() != original.entries().size()
        || index.NodeSize() + 1 != original.children().size() || position >= original.children().size()) {
      return std::nullopt;
    }
    const auto result = TryAllocateUninitialized(source, index, index.DataSize(), index.NodeSize(), 0);
    if (!result) {
      return std::nullopt;
    }
    const node_type* const node = *result;
    std::uninitialized_copy(original.entries().begin(), original.entries().end(), node->EntryPtr());
    const auto children = original.children();
    std::uninitialized_copy_n(children.begin(), position, node->ChildPtr());
    std::uninitialized_copy(
        children.begin() + static_cast<std::ptrdiff_t>(position + 1), children.end(), node->ChildPtr() + position);
    RetainChildren(node->children());
    return result;
  }

  // The caller establishes that entry has the collision node's full hash.
  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryInsertCollisionEntry(
      Source& source,
      const node_type& original,
      std::size_t position,
      const Entry& entry) noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    if (!original.is_collision() || position > original.entries().size()) {
      return std::nullopt;
    }
    const std::size_t count = original.entries().size() + 1;
    const auto result = TryAllocateUninitialized(source, {}, count, 0, count);
    if (result) {
      CopyInsertedEntries(**result, original.entries(), position, entry);
    }
    return result;
  }

  // Removing the final entry is a tree-level removal, not an empty collision
  // allocation. The caller handles that case by removing the parent slot.
  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryEraseCollisionEntry(
      Source& source,
      const node_type& original,
      std::size_t position) noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    if (!original.is_collision() || original.entries().size() <= 1 || position >= original.entries().size()) {
      return std::nullopt;
    }
    const std::size_t count = original.entries().size() - 1;
    const auto result = TryAllocateUninitialized(source, {}, count, 0, count);
    if (result) {
      CopyErasedEntries(**result, original.entries(), position);
    }
    return result;
  }

  // The caller preserves the entry's routing hash and the container's key
  // uniqueness. This primitive copies storage; it does not reindex a new key.
  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryReplaceEntry(
      Source& source,
      const node_type& original,
      std::size_t position,
      const Entry& replacement) noexcept
  requires std::is_nothrow_copy_constructible_v<Entry>
  {
    if (position >= original.entries().size()) {
      return std::nullopt;
    }
    const auto result = TryAllocateUninitialized(
        source, original.index_, original.entries().size(), original.children().size(), original.collision_count_);
    if (!result) {
      return std::nullopt;
    }
    const node_type* const node = *result;
    const auto entries = original.entries();
    std::uninitialized_copy_n(entries.begin(), position, node->EntryPtr());
    std::construct_at(node->EntryPtr() + position, replacement);
    std::uninitialized_copy(
        entries.begin() + static_cast<std::ptrdiff_t>(position + 1), entries.end(), node->EntryPtr() + position + 1);
    CopyChildren(*node, original.children());
    return result;
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
    for (const node_type* child : children) {
      if (child == nullptr) {
        return std::nullopt;
      }
    }
    const auto result = TryAllocateUninitialized(source, index, entries.size(), children.size(), collision_count);
    if (result) {
      std::uninitialized_copy(entries.begin(), entries.end(), (*result)->EntryPtr());
      CopyChildren(**result, children);
    }
    return result;
  }

  template<mbo::memory::BlockSource Source>
  static std::optional<node_type*> TryAllocateUninitialized(
      Source& source,
      index_type index,
      std::size_t entry_count,
      std::size_t child_count,
      std::size_t collision_count) noexcept {
    const auto layout = Layout::TryMake(entry_count, child_count);
    if (!layout) {
      return std::nullopt;
    }
    const auto block = source.TryAcquire(layout->size, layout->alignment);
    if (!block) {
      return std::nullopt;
    }
    if (!Usable(*block, *layout)) {
      source.Release(*block);
      return std::nullopt;
    }
    auto* const node = std::construct_at(
        reinterpret_cast<node_type*>(block->data),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        index, collision_count);
    node->block_ = *block;
    return node;
  }

  static void CopyChildren(const node_type& node, std::span<node_type* const> children) noexcept {
    std::uninitialized_copy(children.begin(), children.end(), node.ChildPtr());
    RetainChildren(children);
  }

  static void CopyInsertedEntries(
      const node_type& node,
      std::span<const Entry> entries,
      std::size_t position,
      const Entry& entry) noexcept {
    std::uninitialized_copy_n(entries.begin(), position, node.EntryPtr());
    std::construct_at(node.EntryPtr() + position, entry);
    std::uninitialized_copy(
        entries.begin() + static_cast<std::ptrdiff_t>(position), entries.end(), node.EntryPtr() + position + 1);
  }

  static void CopyErasedEntries(const node_type& node, std::span<const Entry> entries, std::size_t position) noexcept {
    std::uninitialized_copy_n(entries.begin(), position, node.EntryPtr());
    std::uninitialized_copy(
        entries.begin() + static_cast<std::ptrdiff_t>(position + 1), entries.end(), node.EntryPtr() + position);
  }

  static void RetainChildren(std::span<node_type* const> children) noexcept {
    for (node_type* child : children) {
      Retain(child);
    }
  }

  static bool Usable(const mbo::memory::MemoryBlock& block, const Layout& layout) noexcept {
    return block.data != nullptr && block.size >= layout.size && block.alignment >= layout.alignment
           && std::bit_cast<std::uintptr_t>(block.data) % layout.alignment == 0;
  }

  Entry* EntryPtr() const noexcept {
    constexpr std::size_t kDataOffset =
        sizeof(node_type) + ((alignof(Entry) - (sizeof(node_type) % alignof(Entry))) % alignof(Entry));
    auto* const bytes = reinterpret_cast<std::byte*>(const_cast<node_type*>(this));  // NOLINT
    return reinterpret_cast<Entry*>(bytes + kDataOffset);                            // NOLINT
  }

  node_type** ChildPtr() const noexcept {
    // TryCreate validates all size arithmetic before constructing this immutable index.
    // The node header and therefore EntryPtr() are already pointer-aligned; only the entry-array size needs padding.
    const std::size_t data_size = EntryCount() * sizeof(Entry);
    auto* const data_end = reinterpret_cast<std::byte*>(EntryPtr()) + data_size;  // NOLINT
    const std::size_t padding = (alignof(node_type*) - (data_size % alignof(node_type*))) % alignof(node_type*);
    return reinterpret_cast<node_type**>(data_end + padding);  // NOLINT
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
