// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_ROOT_OWNER_H_
#define MBO_CONTAINER_INTERNAL_HAMT_ROOT_OWNER_H_

#include <cstddef>
#include <utility>

#include "mbo/container/internal/hamt_shared_node.h"
#include "mbo/memory/block_source.h"

namespace mbo::container::container_internal {

// Owns one snapshot reference and borrows its allocation source. Source must
// outlive this owner and every copy. Construction/reset adopt an already owned
// reference; copying retains, moving transfers and leaves the source empty.
// NOLINTBEGIN(readability-identifier-naming): smart-pointer vocabulary.
template<std::size_t FragmentBits, typename Entry, mbo::memory::BlockSource Source>
class HamtRootOwner final {
 public:
  using node_type = HamtSharedNode<FragmentBits, Entry>;

  explicit HamtRootOwner(Source& source, node_type* owned = nullptr) noexcept : source_(&source), root_(owned) {}

  HamtRootOwner(const HamtRootOwner& other) noexcept : source_(other.source_), root_(other.root_) {
    node_type::Retain(root_);
  }

  HamtRootOwner& operator=(const HamtRootOwner& other) noexcept {
    HamtRootOwner copied(other);
    swap(copied);
    return *this;
  }

  HamtRootOwner(HamtRootOwner&& other) noexcept : source_(other.source_), root_(std::exchange(other.root_, nullptr)) {}

  HamtRootOwner& operator=(HamtRootOwner&& other) noexcept {
    HamtRootOwner moved(std::move(other));
    swap(moved);
    return *this;
  }

  ~HamtRootOwner() {
    if (root_ != nullptr) {
      node_type::Release(*source_, root_);
    }
  }

  node_type* get() const noexcept { return root_; }

  Source& source() const noexcept { return *source_; }

  void reset(node_type* owned = nullptr) noexcept {
    auto* const previous = std::exchange(root_, owned);
    if (previous != nullptr) {
      node_type::Release(*source_, previous);
    }
  }

  [[nodiscard]] node_type* release() noexcept { return std::exchange(root_, nullptr); }

  void swap(HamtRootOwner& other) noexcept {
    std::swap(source_, other.source_);
    std::swap(root_, other.root_);
  }

  friend void swap(HamtRootOwner& lhs, HamtRootOwner& rhs) noexcept { lhs.swap(rhs); }

 private:
  Source* source_;
  node_type* root_;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_ROOT_OWNER_H_
