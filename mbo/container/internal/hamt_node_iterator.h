// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_NODE_ITERATOR_H_
#define MBO_CONTAINER_INTERNAL_HAMT_NODE_ITERATOR_H_

#include <iterator>
#include <type_traits>
#include <utility>

namespace mbo::container::container_internal {

// Adapts the existing HAMT traversal to separately owned node values, without
// exposing payload ownership handles or changing traversal/identity semantics.
// NOLINTBEGIN(readability-identifier-naming): STL iterator vocabulary.
template<std::forward_iterator Iterator>
class HamtNodeIterator final {
  static_assert(std::is_nothrow_default_constructible_v<Iterator>);
  static_assert(std::is_nothrow_copy_constructible_v<Iterator> && std::is_nothrow_move_constructible_v<Iterator>);
  static_assert(std::is_nothrow_destructible_v<Iterator>);
  static_assert(noexcept(++std::declval<Iterator&>()));
  static_assert(noexcept(std::declval<const Iterator&>() == std::declval<const Iterator&>()));
  static_assert(noexcept(std::declval<const Iterator&>()->get()));

 public:
  using pointer = decltype(std::declval<const Iterator&>()->get());
  using reference = decltype(*std::declval<pointer>());
  using value_type = std::remove_cvref_t<reference>;
  using difference_type = std::iterator_traits<Iterator>::difference_type;
  using iterator_category = std::forward_iterator_tag;
  using iterator_concept = std::forward_iterator_tag;

  HamtNodeIterator() noexcept = default;

  explicit HamtNodeIterator(Iterator iterator) noexcept : iterator_(std::move(iterator)) {}

  reference operator*() const noexcept { return *iterator_->get(); }

  pointer operator->() const noexcept { return iterator_->get(); }

  HamtNodeIterator& operator++() noexcept {
    ++iterator_;
    return *this;
  }

  HamtNodeIterator operator++(int) noexcept {
    auto before = *this;
    ++*this;
    return before;
  }

  friend bool operator==(const HamtNodeIterator&, const HamtNodeIterator&) noexcept = default;

 private:
  Iterator iterator_{};
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_NODE_ITERATOR_H_
