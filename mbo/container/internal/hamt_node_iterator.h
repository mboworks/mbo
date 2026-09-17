// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_HAMT_NODE_ITERATOR_H_
#define MBO_CONTAINER_INTERNAL_HAMT_NODE_ITERATOR_H_

#include <concepts>
#include <iterator>
#include <type_traits>
#include <utility>

namespace mbo::container::container_internal {

// Adapts the existing HAMT traversal to separately owned node values, without
// exposing payload ownership handles or changing traversal/identity semantics.
// NOLINTBEGIN(readability-identifier-naming): STL iterator vocabulary.
template<std::forward_iterator Iterator, bool Mutable = false>
class HamtNodeIterator final {
  static_assert(std::is_nothrow_default_constructible_v<Iterator>);
  static_assert(std::is_nothrow_copy_constructible_v<Iterator> && std::is_nothrow_move_constructible_v<Iterator>);
  static_assert(std::is_nothrow_destructible_v<Iterator>);
  static_assert(noexcept(++std::declval<Iterator&>()));
  static_assert(noexcept(std::declval<const Iterator&>() == std::declval<const Iterator&>()));

  static auto ValuePointer(const Iterator& iterator) noexcept {
    if constexpr (Mutable) {
      static_assert(noexcept(iterator->get_unique_mutable()));
      return iterator->get_unique_mutable();
    } else {
      static_assert(noexcept(iterator->get()));
      return iterator->get();
    }
  }

 public:
  using pointer = decltype(ValuePointer(std::declval<const Iterator&>()));
  using reference = decltype(*std::declval<pointer>());
  using value_type = std::remove_cvref_t<reference>;
  using difference_type = std::iterator_traits<Iterator>::difference_type;
  using iterator_category = std::forward_iterator_tag;
  using iterator_concept = std::forward_iterator_tag;

  HamtNodeIterator() noexcept = default;

  explicit HamtNodeIterator(Iterator iterator) noexcept : iterator_(std::move(iterator)) {}

  template<std::forward_iterator OtherIterator, bool OtherMutable>
  requires(!Mutable && OtherMutable && std::convertible_to<OtherIterator, Iterator>)
  // Mutable iterators convert implicitly to const iterators, matching standard container iterators.
  // NOLINTNEXTLINE(google-explicit-constructor)
  HamtNodeIterator(const HamtNodeIterator<OtherIterator, OtherMutable>& other) noexcept : iterator_(other.iterator_) {
    static_assert(std::is_nothrow_constructible_v<Iterator, const OtherIterator&>);
  }

  reference operator*() const noexcept { return *ValuePointer(iterator_); }

  pointer operator->() const noexcept { return ValuePointer(iterator_); }

  HamtNodeIterator& operator++() noexcept {
    ++iterator_;
    return *this;
  }

  HamtNodeIterator operator++(int) noexcept {
    auto before = *this;
    ++*this;
    return before;
  }

  template<std::forward_iterator OtherIterator, bool OtherMutable>
  requires requires(const Iterator& first, const OtherIterator& second) {
    { first == second } -> std::convertible_to<bool>;
  }
  bool operator==(const HamtNodeIterator<OtherIterator, OtherMutable>& other) const noexcept {
    static_assert(noexcept(iterator_ == other.iterator_));
    return iterator_ == other.iterator_;
  }

 private:
  template<std::forward_iterator, bool>
  friend class HamtNodeIterator;

  Iterator iterator_{};
};

// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_HAMT_NODE_ITERATOR_H_
