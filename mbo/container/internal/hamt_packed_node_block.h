// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_PACKED_NODE_BLOCK_H_
#define MBO_CONTAINER_INTERNAL_HAMT_PACKED_NODE_BLOCK_H_

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

#include "mbo/config/require.h"
#include "mbo/container/internal/hamt_packed_node_layout.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

// NOLINTBEGIN(readability-identifier-naming): packed storage exposes STL-style span accessors.
template<typename Header, typename Entry, typename Child, mbo::memory::BlockSource Source>
class HamtPackedNodeBlock final {
  static_assert(
      std::is_nothrow_destructible_v<Header> && std::is_nothrow_destructible_v<Entry>
          && std::is_nothrow_destructible_v<Child>,
      "Packed HAMT storage requires non-throwing destruction");

 public:
  constexpr explicit HamtPackedNodeBlock(Source& source) noexcept : source_(std::addressof(source)) {}

  HamtPackedNodeBlock(const HamtPackedNodeBlock&) = delete;
  HamtPackedNodeBlock& operator=(const HamtPackedNodeBlock&) = delete;
  HamtPackedNodeBlock(HamtPackedNodeBlock&&) = delete;
  HamtPackedNodeBlock& operator=(HamtPackedNodeBlock&&) = delete;

  constexpr ~HamtPackedNodeBlock() { clear(); }

  constexpr bool TryInitialize(
      const Header& header,
      std::span<const Entry> entries,
      std::span<const Child> children) noexcept
  requires(
      std::is_nothrow_copy_constructible_v<Header> && std::is_nothrow_copy_constructible_v<Entry>
      && std::is_nothrow_copy_constructible_v<Child>) {
    if (block_.data != nullptr) {
      return false;
    }
    const auto layout = Layout::TryMake(entries.size(), children.size());
    if (!layout) {
      return false;
    }
    const auto block = source_->TryAcquire(layout->size, layout->alignment);
    if (!block) {
      return false;
    }
    if (!Usable(*block, *layout)) {
      source_->Release(*block);
      return false;
    }
    block_ = *block;
    layout_ = *layout;
    entry_count_ = entries.size();
    child_count_ = children.size();
    std::construct_at(HeaderPtr(), header);
    std::uninitialized_copy(entries.begin(), entries.end(), EntryPtr());
    std::uninitialized_copy(children.begin(), children.end(), ChildPtr());
    return true;
  }

  constexpr Header& header() noexcept {
    MBO_CONFIG_REQUIRE(!empty(), "Packed HAMT header access requires an initialized block");
    return *HeaderPtr();
  }

  constexpr const Header& header() const noexcept {
    MBO_CONFIG_REQUIRE(!empty(), "Packed HAMT header access requires an initialized block");
    return *HeaderPtr();
  }

  constexpr std::span<Entry> entries() noexcept {
    return empty() ? std::span<Entry>{} : std::span<Entry>{EntryPtr(), entry_count_};
  }

  constexpr std::span<const Entry> entries() const noexcept {
    return empty() ? std::span<const Entry>{} : std::span<const Entry>{EntryPtr(), entry_count_};
  }

  constexpr std::span<Child> children() noexcept {
    return empty() ? std::span<Child>{} : std::span<Child>{ChildPtr(), child_count_};
  }

  constexpr std::span<const Child> children() const noexcept {
    return empty() ? std::span<const Child>{} : std::span<const Child>{ChildPtr(), child_count_};
  }

  constexpr bool empty() const noexcept { return block_.data == nullptr; }

  constexpr void clear() noexcept {
    if (empty()) {
      return;
    }
    std::destroy_n(ChildPtr(), child_count_);
    std::destroy_n(EntryPtr(), entry_count_);
    std::destroy_at(HeaderPtr());
    source_->Release(block_);
    block_ = {};
    entry_count_ = 0;
    child_count_ = 0;
  }

 private:
  using Layout = HamtPackedNodeLayout<Header, Entry, Child>;

  static constexpr bool Usable(const mbo::memory::MemoryBlock& block, const Layout& layout) noexcept {
    return block.data != nullptr && block.size >= layout.size && block.alignment >= layout.alignment
           && std::bit_cast<std::uintptr_t>(block.data) % layout.alignment == 0;
  }

  constexpr Header* HeaderPtr() const noexcept {
    return reinterpret_cast<Header*>(block_.data);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
  }

  constexpr Entry* EntryPtr() const noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): checked aligned packed object storage.
    return reinterpret_cast<Entry*>(block_.data + layout_.data_offset);
  }

  constexpr Child* ChildPtr() const noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): checked aligned packed object storage.
    return reinterpret_cast<Child*>(block_.data + layout_.child_offset);
  }

  Source* source_;
  mbo::memory::MemoryBlock block_{};
  Layout layout_{};
  std::size_t entry_count_ = 0;
  std::size_t child_count_ = 0;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_PACKED_NODE_BLOCK_H_
