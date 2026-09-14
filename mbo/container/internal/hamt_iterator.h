// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_ITERATOR_H_
#define MBO_CONTAINER_INTERNAL_HAMT_ITERATOR_H_

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>

#include "mbo/container/internal/hamt_hash_path.h"
#include "mbo/container/internal/hamt_shared_node.h"

namespace mbo::container::container_internal {

// Borrows a valid HAMT: its root and descendants must stay alive. Mutable
// instantiations require exclusive ownership of every node before construction.
// Active iterators compare within a root's range, not merely by shared entry
// address. Exhausted and value-initialized iterators share the end value.
// NOLINTBEGIN(readability-identifier-naming) -- STL iterator vocabulary.
template<std::size_t FragmentBits, typename Entry, bool Mutable = false>
class HamtIterator final {
 public:
  using value_type = Entry;
  using difference_type = std::ptrdiff_t;
  using reference = std::conditional_t<Mutable, Entry&, const Entry&>;
  using pointer = std::conditional_t<Mutable, Entry*, const Entry*>;
  using node_type =
      std::conditional_t<Mutable, HamtSharedNode<FragmentBits, Entry>, const HamtSharedNode<FragmentBits, Entry>>;
  using iterator_category = std::forward_iterator_tag;
  using iterator_concept = std::forward_iterator_tag;

  constexpr HamtIterator() noexcept = default;

  template<bool OtherMutable>
  requires(!Mutable && OtherMutable)
  HamtIterator(const HamtIterator<FragmentBits, Entry, OtherMutable>& other) noexcept
      : depth_(other.depth_), current_(other.current_), range_(other.range_) {
    for (std::size_t index = 0; index < depth_; ++index) {
      const auto& frame = other.frames_.at(index);
      frames_.at(index) = Frame{.node = frame.node, .entry = frame.entry, .child = frame.child};
    }
  }

  explicit HamtIterator(node_type* root) noexcept : HamtIterator(root, root) {}

  // A non-null range identity distinguishes container objects sharing one root.
  HamtIterator(node_type* root, const void* range) noexcept : range_(root != nullptr ? range : nullptr) {
    if (root != nullptr) {
      frames_.front().node = root;
      depth_ = 1;
      Seek();
    }
  }

  reference operator*() const noexcept { return *current_; }

  // Positions by hash path rather than scanning the range. The target must be
  // the exact borrowed entry address; missing or mismatched targets yield end.
  // Collision buckets alone require a linear scan. No ownership is acquired.
  template<std::unsigned_integral Hash>
  static HamtIterator At(node_type* root, Hash hash, pointer target, const void* range = nullptr) noexcept {
    HamtIterator result;
    if (root == nullptr || target == nullptr) {
      return result;
    }
    const HamtHashPath<Hash, FragmentBits> path(hash);
    result.range_ = range != nullptr ? range : root;
    result.frames_.front().node = root;
    result.depth_ = 1;
    std::size_t level = 0;
    while (root != nullptr) {
      Frame& frame = result.frames_.at(result.depth_ - 1);
      if (root->is_collision()) {
        std::size_t position = 0;
        for (const Entry& entry : root->entries()) {
          if (std::addressof(entry) == target) {
            frame.entry = position + 1;
            result.current_ = target;
            return result;
          }
          ++position;
        }
        return {};
      }
      if (level >= path.kLevels) {
        return {};
      }
      const std::size_t fragment = path.Fragment(level++);
      switch (root->index().Kind(fragment)) {
        case HamtSlotKind::kEmpty: return {};
        case HamtSlotKind::kData:
          if (std::addressof(root->entries().subspan(root->index().DataIndex(fragment)).front()) == target) {
            frame.entry = root->index().DataIndex(fragment) + 1;
            result.current_ = target;
            return result;
          }
          return {};
        case HamtSlotKind::kNode:
          frame.entry = root->entries().size();
          frame.child = root->index().NodeIndex(fragment) + 1;
          root = root->children().subspan(root->index().NodeIndex(fragment)).front();
          result.frames_.at(result.depth_++).node = root;
          break;
      }
    }
    return {};
  }

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
    return lhs.range_ == rhs.range_ && lhs.current_ == rhs.current_;
  }

  template<bool OtherMutable>
  bool operator==(const HamtIterator<FragmentBits, Entry, OtherMutable>& other) const noexcept {
    return range_ == other.range_ && current_ == other.current_;
  }

 private:
  template<std::size_t, typename, bool>
  friend class HamtIterator;

  static constexpr std::size_t kMaxDepth =
      (std::numeric_limits<std::uintmax_t>::digits + FragmentBits - 1) / FragmentBits + 1;

  struct Frame final {
    node_type* node = nullptr;
    std::size_t entry = 0;
    std::size_t child = 0;
  };

  void Seek() noexcept {
    current_ = nullptr;
    while (depth_ != 0) {
      Frame& frame = frames_.at(depth_ - 1);
      if (frame.entry < frame.node->entries().size()) {
        current_ = &frame.node->entries().subspan(frame.entry++).front();
        return;
      }
      if (frame.child < frame.node->children().size()) {
        node_type* const child = frame.node->children().subspan(frame.child++).front();
        frames_.at(depth_++) = Frame{.node = child};
        continue;
      }
      --depth_;
    }
    range_ = nullptr;
  }

  std::array<Frame, kMaxDepth> frames_{};
  std::size_t depth_ = 0;
  pointer current_ = nullptr;
  const void* range_ = nullptr;
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_ITERATOR_H_
