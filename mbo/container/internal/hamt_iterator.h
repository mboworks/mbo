// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_ITERATOR_H_
#define MBO_CONTAINER_INTERNAL_HAMT_ITERATOR_H_

#include <array>
#include <cstddef>
#include <iterator>
#include <limits>

#include "mbo/container/internal/hamt_shared_node.h"

namespace mbo::container::container_internal {

template<std::size_t FragmentBits, typename Entry>
class HamtIterator final {
 public:
  using value_type = Entry;
  using difference_type = std::ptrdiff_t;
  using reference = const Entry&;
  using pointer = const Entry*;
  using iterator_category = std::forward_iterator_tag;
  using iterator_concept = std::forward_iterator_tag;

  constexpr HamtIterator() noexcept = default;

  explicit HamtIterator(const HamtSharedNode<FragmentBits, Entry>* root) noexcept {
    if (root != nullptr) {
      frames_[0].node = root;
      depth_ = 1;
      Seek();
    }
  }

  reference operator*() const noexcept { return *current_; }

  pointer operator->() const noexcept { return current_; }

  HamtIterator& operator++() noexcept {
    Seek();
    return *this;
  }

  HamtIterator operator++(int) noexcept {
    HamtIterator before = *this;
    ++*this;
    return before;
  }

  friend bool operator==(const HamtIterator& lhs, const HamtIterator& rhs) noexcept {
    return lhs.current_ == rhs.current_;
  }

 private:
  using Node = HamtSharedNode<FragmentBits, Entry>;
  static constexpr std::size_t kMaxDepth =
      (std::numeric_limits<std::size_t>::digits + FragmentBits - 1) / FragmentBits + 1;

  struct Frame final {
    const Node* node = nullptr;
    std::size_t entry = 0;
    std::size_t child = 0;
  };

  void Seek() noexcept {
    current_ = nullptr;
    while (depth_ != 0) {
      Frame& frame = frames_[depth_ - 1];
      if (frame.entry < frame.node->entries().size()) {
        current_ = &frame.node->entries()[frame.entry++];
        return;
      }
      if (frame.child < frame.node->children().size()) {
        const Node* const child = frame.node->children()[frame.child++];
        frames_[depth_++] = Frame{.node = child};
        continue;
      }
      --depth_;
    }
  }

  std::array<Frame, kMaxDepth> frames_{};
  std::size_t depth_ = 0;
  pointer current_ = nullptr;
};

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_ITERATOR_H_
