// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_NODE_INDEX_H_
#define MBO_CONTAINER_INTERNAL_HAMT_NODE_INDEX_H_

#include <cstddef>

#include "mbo/container/internal/hamt_bitmap.h"

namespace mbo::container::container_internal {

enum class HamtSlotKind { kEmpty, kData, kNode };

// CHAMP-style independent data and child-node occupancy. A logical slot may
// contain data or a child, never both. Rank maps that slot to its dense array.
template<std::size_t FragmentBits>
requires(FragmentBits >= 4 && FragmentBits <= 7)
class HamtNodeIndex final {
 public:
  static constexpr std::size_t kSlotCount = HamtBitmap<FragmentBits>::kSlotCount;

  constexpr HamtSlotKind kind(std::size_t slot) const noexcept {
    if (data_.contains(slot)) {
      return HamtSlotKind::kData;
    }
    return nodes_.contains(slot) ? HamtSlotKind::kNode : HamtSlotKind::kEmpty;
  }

  constexpr std::size_t data_index(std::size_t slot) const noexcept { return data_.rank(slot); }

  constexpr std::size_t node_index(std::size_t slot) const noexcept { return nodes_.rank(slot); }

  constexpr std::size_t data_size() const noexcept { return data_.size(); }

  constexpr std::size_t node_size() const noexcept { return nodes_.size(); }

  constexpr bool insert_data(std::size_t slot) noexcept { return !nodes_.contains(slot) && data_.set(slot); }

  constexpr bool insert_node(std::size_t slot) noexcept { return !data_.contains(slot) && nodes_.set(slot); }

  constexpr bool erase_data(std::size_t slot) noexcept { return data_.reset(slot); }

  constexpr bool erase_node(std::size_t slot) noexcept { return nodes_.reset(slot); }

  constexpr bool promote_data_to_node(std::size_t slot) noexcept {
    if (!data_.reset(slot)) {
      return false;
    }
    nodes_.set(slot);
    return true;
  }

  constexpr bool demote_node_to_data(std::size_t slot) noexcept {
    if (!nodes_.reset(slot)) {
      return false;
    }
    data_.set(slot);
    return true;
  }

 private:
  HamtBitmap<FragmentBits> data_;
  HamtBitmap<FragmentBits> nodes_;
};

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_NODE_INDEX_H_
