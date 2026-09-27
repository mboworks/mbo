// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_INTERNAL_EXPERIMENTAL_CIRCULAR_BUFFER_H_
#define MBO_CONTAINER_INTERNAL_EXPERIMENTAL_CIRCULAR_BUFFER_H_

#include <algorithm>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>

#include "mbo/config/config.h"
#include "mbo/config/require.h"

namespace mbo::container::container_internal {

// NOLINTBEGIN(readability-identifier-naming): STL container, iterator, and allocator interface.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic): indexed allocator-owned storage.

// Internal, experimental, growable ring. Only [begin_, end_) contains live T objects;
// the unsigned counters wrap, and their difference distinguishes full from empty.
// Capacity is zero or a power of two. Growth preserves contents, never overwrites them.
// See EXPERIMENTAL_CIRCULAR_BUFFER.md for invalidation and exception guarantees.
template<typename T, typename Allocator = std::allocator<T>>
requires(
    std::is_object_v<T> && !std::is_array_v<T> && std::same_as<T, std::remove_cv_t<T>>
    && std::is_nothrow_destructible_v<T>)
class ExperimentalCircularBuffer final {
 private:
  using Traits = std::allocator_traits<Allocator>;
  static_assert(std::same_as<T, typename Traits::value_type>);
  static constexpr bool kRequireThrows = ::mbo::config::kRequireThrows;
  static constexpr bool kRelocatable = std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>;
  static constexpr bool kSwapNoexcept = [] {
    if constexpr (Traits::propagate_on_container_swap::value) {
      return std::is_nothrow_swappable_v<Allocator>;
    } else {
      return Traits::is_always_equal::value;
    }
  }();

  template<bool IsConst>
  class Iterator final {
   private:
    using Owner = std::conditional_t<IsConst, const ExperimentalCircularBuffer, ExperimentalCircularBuffer>;
    template<bool>
    friend class Iterator;
    friend class ExperimentalCircularBuffer;

    constexpr Iterator(Owner* owner, std::size_t pos) noexcept : owner_(owner), pos_(pos) {}

   public:
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::random_access_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using reference = std::conditional_t<IsConst, const T&, T&>;
    using pointer = std::conditional_t<IsConst, const T*, T*>;

    constexpr Iterator() noexcept = default;
    constexpr Iterator(const Iterator&) noexcept = default;
    constexpr Iterator& operator=(const Iterator&) noexcept = default;
    constexpr Iterator(Iterator&&) noexcept = default;
    constexpr Iterator& operator=(Iterator&&) noexcept = default;
    ~Iterator() = default;

    template<bool OtherConst>
    requires(IsConst && !OtherConst)
    // NOLINTNEXTLINE(google-explicit-constructor): standard mutable-to-const iterator conversion.
    constexpr Iterator(const Iterator<OtherConst>& other) noexcept : owner_(other.owner_), pos_(other.pos_) {}

    constexpr reference operator*() const noexcept { return (*owner_)[pos_]; }

    constexpr pointer operator->() const noexcept { return std::addressof(**this); }

    constexpr reference operator[](difference_type offset) const noexcept { return *(*this + offset); }

    constexpr Iterator& operator++() noexcept {
      ++pos_;
      return *this;
    }

    constexpr Iterator operator++(int) noexcept {
      auto result = *this;
      ++*this;
      return result;
    }

    constexpr Iterator& operator--() noexcept {
      --pos_;
      return *this;
    }

    constexpr Iterator operator--(int) noexcept {
      auto result = *this;
      --*this;
      return result;
    }

    constexpr Iterator& operator+=(difference_type offset) noexcept {
      pos_ += static_cast<std::size_t>(offset);
      return *this;
    }

    constexpr Iterator& operator-=(difference_type offset) noexcept {
      pos_ -= static_cast<std::size_t>(offset);
      return *this;
    }

    friend constexpr Iterator operator+(Iterator iter, difference_type offset) noexcept { return iter += offset; }

    friend constexpr Iterator operator+(difference_type offset, Iterator iter) noexcept { return iter += offset; }

    friend constexpr Iterator operator-(Iterator iter, difference_type offset) noexcept { return iter -= offset; }

    friend constexpr difference_type operator-(const Iterator& lhs, const Iterator& rhs) noexcept(!kRequireThrows) {
      MBO_CONFIG_REQUIRE(lhs.owner_ == rhs.owner_, "Cannot subtract iterators from different circular buffers");
      return static_cast<difference_type>(lhs.pos_) - static_cast<difference_type>(rhs.pos_);
    }

    friend constexpr bool operator==(const Iterator& lhs, const Iterator& rhs) noexcept(!kRequireThrows) {
      MBO_CONFIG_REQUIRE(lhs.owner_ == rhs.owner_, "Cannot compare iterators from different circular buffers");
      return lhs.pos_ == rhs.pos_;
    }

    friend constexpr std::strong_ordering operator<=>(const Iterator& lhs, const Iterator& rhs) noexcept(
        !kRequireThrows) {
      MBO_CONFIG_REQUIRE(lhs.owner_ == rhs.owner_, "Cannot order iterators from different circular buffers");
      return lhs.pos_ <=> rhs.pos_;
    }

   private:
    Owner* owner_ = nullptr;
    std::size_t pos_ = 0;
  };

 public:
  using value_type = T;
  using allocator_type = Allocator;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = typename Traits::pointer;
  using const_pointer = typename Traits::const_pointer;
  using iterator = Iterator<false>;
  using const_iterator = Iterator<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  constexpr ExperimentalCircularBuffer() noexcept(std::is_nothrow_default_constructible_v<Allocator>) = default;

  constexpr explicit ExperimentalCircularBuffer(const Allocator& allocator) noexcept(
      std::is_nothrow_copy_constructible_v<Allocator>)
      : allocator_(allocator) {}

  constexpr explicit ExperimentalCircularBuffer(size_type count, const Allocator& allocator = Allocator())
  requires std::default_initializable<T>
      : ExperimentalCircularBuffer(allocator) {
    resize(count);
  }

  constexpr ExperimentalCircularBuffer(size_type count, const T& value, const Allocator& allocator = Allocator())
  requires std::is_copy_constructible_v<T>
      : ExperimentalCircularBuffer(allocator) {
    reserve(count);
    for (size_type index = 0; index < count; ++index) {
      emplace_back(value);
    }
  }

  template<std::input_iterator InputIterator, std::sentinel_for<InputIterator> Sentinel>
  requires std::constructible_from<T, std::iter_reference_t<InputIterator>>
  constexpr ExperimentalCircularBuffer(InputIterator first, Sentinel last, const Allocator& allocator = Allocator())
      : ExperimentalCircularBuffer(allocator) {
    if constexpr (std::sized_sentinel_for<Sentinel, InputIterator>) {
      const auto count = last - first;
      MBO_CONFIG_REQUIRE(std::in_range<size_type>(count), "Circular buffer range size is invalid");
      reserve(static_cast<size_type>(count));
    }
    for (; first != last; ++first) {
      emplace_back(*first);
    }
  }

  constexpr ExperimentalCircularBuffer(std::initializer_list<T> values, const Allocator& allocator = Allocator())
  requires std::is_copy_constructible_v<T>
      : ExperimentalCircularBuffer(values.begin(), values.end(), allocator) {}

  constexpr ExperimentalCircularBuffer(const ExperimentalCircularBuffer& other)
  requires std::is_copy_constructible_v<T>
      : ExperimentalCircularBuffer(other, Traits::select_on_container_copy_construction(other.allocator_)) {}

  constexpr ExperimentalCircularBuffer(const ExperimentalCircularBuffer& other, const Allocator& allocator)
  requires std::is_copy_constructible_v<T>
      : ExperimentalCircularBuffer(allocator) {
    // Retain reservation as well as live values, including an empty reserved ring.
    reserve(other.capacity());
    for (const T& value : other) {
      emplace_back(value);
    }
  }

  constexpr ExperimentalCircularBuffer(ExperimentalCircularBuffer&& other) noexcept(
      std::is_nothrow_move_constructible_v<Allocator>)
      : allocator_(std::move(other.allocator_)) {
    SwapStorage(other);
  }

  constexpr ExperimentalCircularBuffer(ExperimentalCircularBuffer&& other, const Allocator& allocator)
      : ExperimentalCircularBuffer(allocator) {
    if (AllocatorsEqual(other)) {
      SwapStorage(other);
    } else {
      reserve(other.capacity());
      other.RelocateInto(*this);
      other.clear();
    }
  }

  constexpr ~ExperimentalCircularBuffer() { Release(); }

  constexpr ExperimentalCircularBuffer& operator=(const ExperimentalCircularBuffer& other)
  requires std::is_copy_constructible_v<T> {
    if (this != &other) {
      if constexpr (Traits::propagate_on_container_copy_assignment::value) {
        ExperimentalCircularBuffer replacement(other, other.allocator_);
        Release();
        allocator_ = other.allocator_;
        SwapStorage(replacement);
      } else {
        ExperimentalCircularBuffer replacement(other, allocator_);
        SwapStorage(replacement);
      }
    }
    return *this;
  }

  constexpr ExperimentalCircularBuffer& operator=(ExperimentalCircularBuffer&& other) noexcept(
      Traits::propagate_on_container_move_assignment::value ? std::is_nothrow_move_assignable_v<Allocator>
                                                            : Traits::is_always_equal::value) {
    if (this != &other) {
      if constexpr (Traits::propagate_on_container_move_assignment::value) {
        Release();
        allocator_ = std::move(other.allocator_);
        SwapStorage(other);
      } else if (AllocatorsEqual(other)) {
        Release();
        SwapStorage(other);
      } else {
        ExperimentalCircularBuffer replacement(std::move(other), allocator_);
        SwapStorage(replacement);
      }
    }
    return *this;
  }

  constexpr ExperimentalCircularBuffer& operator=(std::initializer_list<T> values)
  requires std::is_copy_constructible_v<T> {
    assign(values);
    return *this;
  }

  constexpr allocator_type get_allocator() const noexcept(std::is_nothrow_copy_constructible_v<Allocator>) {
    return allocator_;
  }

  constexpr bool empty() const noexcept { return begin_ == end_; }

  constexpr bool full() const noexcept { return size() == capacity_; }

  constexpr size_type size() const noexcept { return end_ - begin_; }

  constexpr size_type capacity() const noexcept { return capacity_; }

  constexpr size_type max_size() const noexcept {
    return std::bit_floor(
        std::min(
            static_cast<size_type>(Traits::max_size(allocator_)),
            static_cast<size_type>(std::numeric_limits<difference_type>::max())));
  }

  constexpr reference operator[](size_type pos) noexcept { return *Slot(begin_ + pos); }

  constexpr const_reference operator[](size_type pos) const noexcept { return *Slot(begin_ + pos); }

  constexpr reference at(size_type pos) noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(pos < size(), "Circular buffer index is out of range");
    return (*this)[pos];
  }

  constexpr const_reference at(size_type pos) const noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(pos < size(), "Circular buffer index is out of range");
    return (*this)[pos];
  }

  constexpr reference front() noexcept(!kRequireThrows) { return at(0); }

  constexpr const_reference front() const noexcept(!kRequireThrows) { return at(0); }

  constexpr reference back() noexcept(!kRequireThrows) { return at(size() - 1); }

  constexpr const_reference back() const noexcept(!kRequireThrows) { return at(size() - 1); }

  constexpr iterator begin() noexcept { return iterator(this, 0); }

  constexpr const_iterator begin() const noexcept { return const_iterator(this, 0); }

  constexpr const_iterator cbegin() const noexcept { return begin(); }

  constexpr iterator end() noexcept { return iterator(this, size()); }

  constexpr const_iterator end() const noexcept { return const_iterator(this, size()); }

  constexpr const_iterator cend() const noexcept { return end(); }

  constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }

  constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }

  constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }

  constexpr reverse_iterator rend() noexcept { return reverse_iterator(begin()); }

  constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

  constexpr const_reverse_iterator crend() const noexcept { return rend(); }

  constexpr void reserve(size_type requested) {
    MBO_CONFIG_REQUIRE(requested <= max_size(), "Circular buffer capacity exceeds max_size");
    if (requested > capacity_) {
      Reallocate(std::bit_ceil(requested));
    }
  }

  constexpr void shrink_to_fit() {
    const size_type requested = empty() ? 0 : std::bit_ceil(size());
    if (requested < capacity_) {
      Reallocate(requested);
    }
  }

  constexpr void clear() noexcept {
    for (T& value : *this) {
      Traits::destroy(allocator_, std::addressof(value));
    }
    begin_ = 0;
    end_ = 0;
  }

  template<typename... Args>
  requires std::constructible_from<T, Args...>
  constexpr reference emplace_back(Args&&... args) {
    if (full()) {
      return GrowAndEmplace<false>(std::forward<Args>(args)...);
    }
    T* const result = Slot(end_);
    Traits::construct(allocator_, result, std::forward<Args>(args)...);
    ++end_;
    return *result;
  }

  template<typename... Args>
  requires std::constructible_from<T, Args...>
  constexpr reference emplace_front(Args&&... args) {
    if (full()) {
      return GrowAndEmplace<true>(std::forward<Args>(args)...);
    }
    T* const result = Slot(begin_ - 1);
    Traits::construct(allocator_, result, std::forward<Args>(args)...);
    --begin_;
    return *result;
  }

  constexpr void push_back(const T& value)
  requires std::is_copy_constructible_v<T> {
    emplace_back(value);
  }

  constexpr void push_back(T&& value)
  requires std::is_move_constructible_v<T> {
    emplace_back(std::move(value));
  }

  constexpr void push_front(const T& value)
  requires std::is_copy_constructible_v<T> {
    emplace_front(value);
  }

  constexpr void push_front(T&& value)
  requires std::is_move_constructible_v<T> {
    emplace_front(std::move(value));
  }

  constexpr void pop_back() noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(!empty(), "Cannot pop an empty circular buffer");
    Traits::destroy(allocator_, Slot(end_ - 1));
    --end_;
  }

  constexpr void pop_front() noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(!empty(), "Cannot pop an empty circular buffer");
    Traits::destroy(allocator_, Slot(begin_));
    ++begin_;
  }

  template<typename... Args>
  requires std::constructible_from<T, Args...>
  constexpr iterator emplace(const_iterator pos, Args&&... args) {
    const size_type index = Position(pos);
    if (index == size()) {
      emplace_back(std::forward<Args>(args)...);
    } else if (index == 0) {
      emplace_front(std::forward<Args>(args)...);
    } else {
      ExperimentalCircularBuffer inserted(allocator_);
      inserted.emplace_back(std::forward<Args>(args)...);
      return InsertStaged(index, inserted);
    }
    return iterator(this, index);
  }

  constexpr iterator insert(const_iterator pos, const T& value)
  requires std::is_copy_constructible_v<T> {
    return emplace(pos, value);
  }

  constexpr iterator insert(const_iterator pos, T&& value)
  requires std::is_move_constructible_v<T> {
    return emplace(pos, std::move(value));
  }

  constexpr iterator insert(const_iterator pos, size_type count, const T& value)
  requires std::is_copy_constructible_v<T> {
    const size_type index = Position(pos);
    MBO_CONFIG_REQUIRE(count <= max_size() - size(), "Circular buffer insertion exceeds max_size");
    ExperimentalCircularBuffer inserted(count, value, allocator_);
    return InsertStaged(index, inserted);
  }

  template<std::input_iterator InputIterator, std::sentinel_for<InputIterator> Sentinel>
  requires std::constructible_from<T, std::iter_reference_t<InputIterator>>
  constexpr iterator insert(const_iterator pos, InputIterator first, Sentinel last) {
    const size_type index = Position(pos);
    ExperimentalCircularBuffer inserted(std::move(first), std::move(last), allocator_);
    return InsertStaged(index, inserted);
  }

  constexpr iterator insert(const_iterator pos, std::initializer_list<T> values)
  requires std::is_copy_constructible_v<T> {
    return insert(pos, values.begin(), values.end());
  }

  template<std::ranges::input_range Range>
  requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
  constexpr iterator insert_range(const_iterator pos, Range&& range) {
    return insert(pos, std::ranges::begin(range), std::ranges::end(range));
  }

  template<std::ranges::input_range Range>
  requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
  constexpr void append_range(Range&& range) {
    insert_range(cend(), std::forward<Range>(range));
  }

  template<std::ranges::input_range Range>
  requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
  constexpr void prepend_range(Range&& range) {
    insert_range(cbegin(), std::forward<Range>(range));
  }

  constexpr iterator erase(const_iterator pos)
  requires(std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>) {
    MBO_CONFIG_REQUIRE(Position(pos) < size(), "Cannot erase circular buffer end");
    return erase(pos, pos + 1);
  }

  constexpr iterator erase(const_iterator first, const_iterator last)
  requires(std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>) {
    const size_type start = Position(first);
    const size_type stop = Position(last);
    MBO_CONFIG_REQUIRE(start <= stop, "Circular buffer erase range is reversed");
    const size_type count = stop - start;
    if (count == 0) {
      return iterator(this, start);
    }
    if (start == 0) {
      for (size_type index = 0; index < count; ++index) {
        pop_front();
      }
    } else {
      for (size_type index = stop; index < size(); ++index) {
        if constexpr (std::is_move_assignable_v<T>) {
          (*this)[index - count] = std::move((*this)[index]);
        } else {
          (*this)[index - count] = (*this)[index];
        }
      }
      for (size_type index = 0; index < count; ++index) {
        pop_back();
      }
    }
    return iterator(this, start);
  }

  constexpr void assign(size_type count, const T& value)
  requires std::is_copy_constructible_v<T> {
    ExperimentalCircularBuffer replacement(count, value, allocator_);
    SwapStorage(replacement);
  }

  template<std::input_iterator InputIterator, std::sentinel_for<InputIterator> Sentinel>
  requires std::constructible_from<T, std::iter_reference_t<InputIterator>>
  constexpr void assign(InputIterator first, Sentinel last) {
    ExperimentalCircularBuffer replacement(std::move(first), std::move(last), allocator_);
    SwapStorage(replacement);
  }

  constexpr void assign(std::initializer_list<T> values)
  requires std::is_copy_constructible_v<T> {
    assign(values.begin(), values.end());
  }

  template<std::ranges::input_range Range>
  requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
  constexpr void assign_range(Range&& range) {
    assign(std::ranges::begin(range), std::ranges::end(range));
  }

  constexpr void resize(size_type requested)
  requires std::default_initializable<T> {
    if (requested <= size()) {
      while (size() > requested) {
        pop_back();
      }
      return;
    }
    reserve(requested);
#if __cpp_exceptions
    const size_type original_size = size();
    try {
#endif
      while (size() < requested) {
        emplace_back();
      }
#if __cpp_exceptions
    } catch (...) {
      while (size() > original_size) {
        pop_back();
      }
      throw;
    }
#endif
  }

  constexpr void resize(size_type requested, const T& value)
  requires std::is_copy_constructible_v<T> {
    if (requested > size()) {
      insert(cend(), requested - size(), value);
    } else {
      while (size() > requested) {
        pop_back();
      }
    }
  }

  constexpr void swap(ExperimentalCircularBuffer& other) noexcept(kSwapNoexcept) {
    if constexpr (Traits::propagate_on_container_swap::value) {
      using std::swap;
      swap(allocator_, other.allocator_);
    } else {
      MBO_CONFIG_REQUIRE(AllocatorsEqual(other), "Circular buffer swap requires equal nonpropagating allocators");
    }
    SwapStorage(other);
  }

  friend constexpr void swap(ExperimentalCircularBuffer& lhs, ExperimentalCircularBuffer& rhs) noexcept(
      noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
  }

  friend constexpr bool operator==(const ExperimentalCircularBuffer& lhs, const ExperimentalCircularBuffer& rhs)
  requires std::equality_comparable<T> {
    return std::ranges::equal(lhs, rhs);
  }

  friend constexpr auto operator<=>(const ExperimentalCircularBuffer& lhs, const ExperimentalCircularBuffer& rhs)
  requires std::three_way_comparable<T> {
    return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
  }

 private:
  constexpr T* Slot(size_type coordinate) noexcept {
    return std::to_address(storage_) + (coordinate & (capacity_ - 1));
  }

  constexpr const T* Slot(size_type coordinate) const noexcept {
    return std::to_address(storage_) + (coordinate & (capacity_ - 1));
  }

  constexpr size_type Position(const_iterator pos) const noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(pos.owner_ == this && pos.pos_ <= size(), "Circular buffer iterator is out of range");
    return pos.pos_;
  }

  constexpr bool AllocatorsEqual(const ExperimentalCircularBuffer& other) const noexcept {
    if constexpr (Traits::is_always_equal::value) {
      return true;
    } else {
      return allocator_ == other.allocator_;
    }
  }

  constexpr void SwapStorage(ExperimentalCircularBuffer& other) noexcept {
    using std::swap;
    swap(storage_, other.storage_);
    swap(capacity_, other.capacity_);
    swap(begin_, other.begin_);
    swap(end_, other.end_);
  }

  constexpr void Allocate(size_type count) {
    if (count != 0) {
      storage_ = Traits::allocate(allocator_, count);
      capacity_ = count;
    }
  }

  constexpr void Release() noexcept {
    clear();
    if (capacity_ != 0) {
      Traits::deallocate(allocator_, storage_, capacity_);
      storage_ = pointer();
      capacity_ = 0;
    }
  }

  constexpr void RelocateInto(ExperimentalCircularBuffer& replacement) {
    if constexpr (kRelocatable) {
      for (T& value : *this) {
        replacement.emplace_back(std::move_if_noexcept(value));
      }
    } else {
      MBO_CONFIG_REQUIRE(empty(), "Cannot relocate live immovable circular buffer elements");
    }
  }

  constexpr void Reallocate(size_type count) {
    ExperimentalCircularBuffer replacement(allocator_);
    replacement.Allocate(count);
    RelocateInto(replacement);
    SwapStorage(replacement);
  }

  template<bool Front, typename... Args>
  constexpr reference GrowAndEmplace(Args&&... args) {
    MBO_CONFIG_REQUIRE(size() < max_size(), "Circular buffer insertion exceeds max_size");
    ExperimentalCircularBuffer replacement(allocator_);
    replacement.Allocate(std::bit_ceil(size() + 1));
    // Construct the new value before relocating aliased arguments from this ring.
    if constexpr (Front) {
      replacement.emplace_back(std::forward<Args>(args)...);
      RelocateInto(replacement);
    } else {
      replacement.begin_ = size();
      replacement.end_ = size();
      replacement.emplace_back(std::forward<Args>(args)...);
      if constexpr (kRelocatable) {
        for (auto iter = rbegin(); iter != rend(); ++iter) {
          replacement.emplace_front(std::move_if_noexcept(*iter));
        }
      } else {
        MBO_CONFIG_REQUIRE(empty(), "Cannot relocate live immovable circular buffer elements");
      }
    }
    SwapStorage(replacement);
    return Front ? front() : back();
  }

  constexpr iterator InsertStaged(size_type pos, ExperimentalCircularBuffer& inserted) {
    if (inserted.empty()) {
      return iterator(this, pos);
    }
    MBO_CONFIG_REQUIRE(inserted.size() <= max_size() - size(), "Circular buffer insertion exceeds max_size");
    ExperimentalCircularBuffer replacement(allocator_);
    replacement.reserve(std::max(capacity_, std::bit_ceil(size() + inserted.size())));
    replacement.begin_ = pos;
    replacement.end_ = pos;
    inserted.RelocateInto(replacement);
    if constexpr (kRelocatable) {
      for (size_type index = pos; index != 0; --index) {
        replacement.emplace_front(std::move_if_noexcept((*this)[index - 1]));
      }
      for (size_type index = pos; index < size(); ++index) {
        replacement.emplace_back(std::move_if_noexcept((*this)[index]));
      }
    } else {
      MBO_CONFIG_REQUIRE(empty(), "Cannot relocate live immovable circular buffer elements");
    }
    SwapStorage(replacement);
    return iterator(this, pos);
  }

  [[no_unique_address]] Allocator allocator_{};
  pointer storage_{};
  size_type capacity_ = 0;
  size_type begin_ = 0;
  size_type end_ = 0;
};

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// NOLINTEND(readability-identifier-naming)

}  // namespace mbo::container::container_internal

#endif  // MBO_CONTAINER_INTERNAL_EXPERIMENTAL_CIRCULAR_BUFFER_H_
