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

  constexpr HamtSlotKind Kind(std::size_t slot) const noexcept {
    if (data_.Contains(slot)) {
      return HamtSlotKind::kData;
    }
    return nodes_.Contains(slot) ? HamtSlotKind::kNode : HamtSlotKind::kEmpty;
  }

  constexpr std::size_t DataIndex(std::size_t slot) const noexcept { return data_.Rank(slot); }

  constexpr std::size_t NodeIndex(std::size_t slot) const noexcept { return nodes_.Rank(slot); }

  constexpr std::size_t DataSize() const noexcept { return data_.Size(); }

  constexpr std::size_t NodeSize() const noexcept { return nodes_.Size(); }

  constexpr bool InsertData(std::size_t slot) noexcept { return !nodes_.Contains(slot) && data_.Set(slot); }

  constexpr bool InsertNode(std::size_t slot) noexcept { return !data_.Contains(slot) && nodes_.Set(slot); }

  constexpr bool EraseData(std::size_t slot) noexcept { return data_.Reset(slot); }

  constexpr bool EraseNode(std::size_t slot) noexcept { return nodes_.Reset(slot); }

  constexpr bool PromoteDataToNode(std::size_t slot) noexcept {
    if (!data_.Reset(slot)) {
      return false;
    }
    nodes_.Set(slot);
    return true;
  }

  constexpr bool DemoteNodeToData(std::size_t slot) noexcept {
    if (!nodes_.Reset(slot)) {
      return false;
    }
    data_.Set(slot);
    return true;
  }

 private:
  HamtBitmap<FragmentBits> data_;
  HamtBitmap<FragmentBits> nodes_;
};

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_NODE_INDEX_H_
