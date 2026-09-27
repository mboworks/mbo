// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_SEGMENTED_DEQUE_H_
#define MBO_CONTAINER_SEGMENTED_DEQUE_H_

#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

#include "mbo/config/config.h"
#include "mbo/config/require.h"
#include "mbo/container/internal/experimental_circular_buffer.h"
#include "mbo/container/segmented_options.h"
#include "mbo/memory/block_source.h"

namespace mbo::container {

// NOLINTBEGIN(readability-identifier-naming): SegmentedDeque models the STL container interface.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access):
// bounded internal storage and checked logical positions.

template<
    SegmentedElement T,
    SegmentedOptions Options = {},
    mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource,
    typename DirectoryAllocator = std::allocator<std::byte>>
requires RepresentableSegmentedOptions<T, Options>
class SegmentedDeque final {
 private:
  static constexpr bool kRequireThrows = ::mbo::config::kRequireThrows;
  static constexpr bool kFiniteDirectory = Options.segment_capacity != std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t kSegmentShift = std::countr_zero(Options.segment_size);
  static constexpr std::size_t kSegmentMask = Options.segment_size - 1;

  union Slot final {
    constexpr Slot() noexcept {}

    Slot(const Slot&) = delete;
    Slot& operator=(const Slot&) = delete;
    Slot(Slot&&) = delete;
    Slot& operator=(Slot&&) = delete;

    constexpr ~Slot() noexcept {}

    T value;
  };

  struct Segment final {
    constexpr explicit Segment(mbo::memory::MemoryBlock memory) noexcept : block(memory) {}

    constexpr T& operator[](std::size_t pos) noexcept { return slots[pos].value; }

    constexpr const T& operator[](std::size_t pos) const noexcept { return slots[pos].value; }

    mbo::memory::MemoryBlock block{};
    Segment* next_spare = nullptr;
    std::array<Slot, Options.segment_size> slots;
  };

  using SegmentPointerAllocator = std::allocator_traits<DirectoryAllocator>::template rebind_alloc<Segment*>;
  using DirectoryAllocatorTraits = std::allocator_traits<DirectoryAllocator>;
  using SegmentPointerAllocatorTraits = std::allocator_traits<SegmentPointerAllocator>;
  using Directory = container_internal::ExperimentalCircularBuffer<Segment*, SegmentPointerAllocator>;
  static constexpr bool kDirectorySwapAlwaysSafe = SegmentPointerAllocatorTraits::propagate_on_container_swap::value
                                                   || SegmentPointerAllocatorTraits::is_always_equal::value;
  static constexpr bool kDirectoryMoveAssignmentAlwaysSafe =
      SegmentPointerAllocatorTraits::propagate_on_container_move_assignment::value
      || SegmentPointerAllocatorTraits::is_always_equal::value;

  struct CopyWithAllocatorTag final {};

  template<bool IsConst>
  class Iterator final {
   private:
    using Owner = std::conditional_t<IsConst, const SegmentedDeque, SegmentedDeque>;

    template<bool>
    friend class Iterator;
    friend class SegmentedDeque;

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
    // This is the standard mutable-iterator to const-iterator conversion.
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr Iterator(const Iterator<OtherConst>& other) noexcept : owner_(other.owner_), pos_(other.pos_) {}

    constexpr reference operator*() const { return owner_->AtCoordinate(pos_); }

    constexpr pointer operator->() const { return std::addressof(**this); }

    constexpr reference operator[](difference_type offset) const { return *(*this + offset); }

    constexpr Iterator& operator++() noexcept {
      ++pos_;
      return *this;
    }

    constexpr Iterator operator++(int) noexcept {
      Iterator result = *this;
      ++*this;
      return result;
    }

    constexpr Iterator& operator--() noexcept {
      --pos_;
      return *this;
    }

    constexpr Iterator operator--(int) noexcept {
      Iterator result = *this;
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

    friend constexpr Iterator operator+(Iterator iterator, difference_type offset) noexcept {
      iterator += offset;
      return iterator;
    }

    friend constexpr Iterator operator+(difference_type offset, Iterator iterator) noexcept {
      return iterator + offset;
    }

    friend constexpr Iterator operator-(Iterator iterator, difference_type offset) noexcept {
      iterator -= offset;
      return iterator;
    }

    friend constexpr difference_type operator-(const Iterator& lhs, const Iterator& rhs) noexcept(!kRequireThrows) {
      MBO_CONFIG_REQUIRE(lhs.owner_ == rhs.owner_, "Cannot subtract iterators from different sequences");
      return static_cast<difference_type>(lhs.Position()) - static_cast<difference_type>(rhs.Position());
    }

    friend constexpr bool operator==(const Iterator& lhs, const Iterator& rhs) noexcept(!kRequireThrows) {
      MBO_CONFIG_REQUIRE(lhs.owner_ == rhs.owner_, "Cannot compare iterators from different sequences");
      return lhs.pos_ == rhs.pos_;
    }

    friend constexpr auto operator<=>(const Iterator& lhs, const Iterator& rhs) noexcept(!kRequireThrows) {
      MBO_CONFIG_REQUIRE(lhs.owner_ == rhs.owner_, "Cannot compare iterators from different sequences");
      return lhs.Position() <=> rhs.Position();
    }

   private:
    constexpr std::size_t Position() const noexcept { return owner_ == nullptr ? 0 : pos_ - owner_->origin_; }

    Owner* owner_ = nullptr;
    std::size_t pos_ = 0;
  };

  template<bool IsConst>
  class SegmentView final {
   private:
    using SegmentType = std::conditional_t<IsConst, const Segment, Segment>;

    class ViewIterator final {
     public:
      using iterator_category = std::random_access_iterator_tag;
      using iterator_concept = std::random_access_iterator_tag;
      using value_type = T;
      using difference_type = std::ptrdiff_t;
      using reference = std::conditional_t<IsConst, const T&, T&>;
      using pointer = std::conditional_t<IsConst, const T*, T*>;

      constexpr ViewIterator() noexcept = default;

      constexpr ViewIterator(SegmentType* segment, std::size_t pos) noexcept : segment_(segment), pos_(pos) {}

      constexpr reference operator*() const noexcept { return (*segment_)[pos_]; }

      constexpr pointer operator->() const noexcept { return std::addressof(**this); }

      constexpr reference operator[](difference_type offset) const noexcept { return *(*this + offset); }

      constexpr ViewIterator& operator++() noexcept {
        ++pos_;
        return *this;
      }

      constexpr ViewIterator operator++(int) noexcept {
        ViewIterator result = *this;
        ++*this;
        return result;
      }

      constexpr ViewIterator& operator--() noexcept {
        --pos_;
        return *this;
      }

      constexpr ViewIterator operator--(int) noexcept {
        ViewIterator result = *this;
        --*this;
        return result;
      }

      constexpr ViewIterator& operator+=(difference_type offset) noexcept {
        pos_ += static_cast<std::size_t>(offset);
        return *this;
      }

      constexpr ViewIterator& operator-=(difference_type offset) noexcept { return *this += -offset; }

      friend constexpr ViewIterator operator+(ViewIterator iterator, difference_type offset) noexcept {
        iterator += offset;
        return iterator;
      }

      friend constexpr ViewIterator operator+(difference_type offset, ViewIterator iterator) noexcept {
        return iterator + offset;
      }

      friend constexpr ViewIterator operator-(ViewIterator iterator, difference_type offset) noexcept {
        iterator -= offset;
        return iterator;
      }

      friend constexpr difference_type operator-(const ViewIterator& lhs, const ViewIterator& rhs) noexcept {
        return static_cast<difference_type>(lhs.pos_) - static_cast<difference_type>(rhs.pos_);
      }

      friend constexpr bool operator==(const ViewIterator&, const ViewIterator&) noexcept = default;

      friend constexpr auto operator<=>(const ViewIterator& lhs, const ViewIterator& rhs) noexcept {
        return lhs.pos_ <=> rhs.pos_;
      }

     private:
      SegmentType* segment_ = nullptr;
      std::size_t pos_ = 0;
    };

    friend class SegmentedDeque;

    constexpr SegmentView(SegmentType* segment, std::size_t first, std::size_t count) noexcept
        : segment_(segment), first_(first), size_(count) {}

   public:
    using iterator = ViewIterator;

    constexpr iterator begin() const noexcept { return iterator(segment_, first_); }

    constexpr iterator end() const noexcept { return iterator(segment_, first_ + size_); }

    constexpr decltype(auto) operator[](std::size_t pos) const noexcept { return (*segment_)[first_ + pos]; }

    constexpr std::size_t size() const noexcept { return size_; }

    constexpr bool empty() const noexcept { return size() == 0; }

   private:
    SegmentType* segment_ = nullptr;
    std::size_t first_ = 0;
    std::size_t size_ = 0;
  };

  template<bool IsConst>
  class SegmentRange final {
   private:
    using Owner = std::conditional_t<IsConst, const SegmentedDeque, SegmentedDeque>;
    using View = SegmentView<IsConst>;

    class SegmentIterator final {
     public:
      using iterator_category = std::forward_iterator_tag;
      using iterator_concept = std::forward_iterator_tag;
      using value_type = View;
      using difference_type = std::ptrdiff_t;

      constexpr SegmentIterator() noexcept = default;

      constexpr SegmentIterator(Owner* owner, std::size_t pos) noexcept : owner_(owner), pos_(pos) {}

      constexpr value_type operator*() const noexcept { return owner_->template MakeSegmentView<IsConst>(pos_); }

      constexpr SegmentIterator& operator++() noexcept {
        ++pos_;
        return *this;
      }

      constexpr SegmentIterator operator++(int) noexcept {
        SegmentIterator result = *this;
        ++*this;
        return result;
      }

      friend constexpr bool operator==(const SegmentIterator&, const SegmentIterator&) noexcept = default;

     private:
      Owner* owner_ = nullptr;
      std::size_t pos_ = 0;
    };

    friend class SegmentedDeque;

    constexpr SegmentRange(Owner* owner, std::size_t size) noexcept : owner_(owner), size_(size) {}

   public:
    constexpr SegmentIterator begin() const noexcept { return SegmentIterator(owner_, 0); }

    constexpr SegmentIterator end() const noexcept { return SegmentIterator(owner_, size_); }

    constexpr View operator[](std::size_t pos) const noexcept { return owner_->template MakeSegmentView<IsConst>(pos); }

    constexpr std::size_t size() const noexcept { return size_; }

    constexpr bool empty() const noexcept { return size_ == 0; }

   private:
    Owner* owner_ = nullptr;
    std::size_t size_ = 0;
  };

 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using iterator = Iterator<false>;
  using const_iterator = Iterator<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using allocator_type = DirectoryAllocator;
  using segment_view = SegmentView<false>;
  using const_segment_view = SegmentView<true>;
  using segment_range = SegmentRange<false>;
  using const_segment_range = SegmentRange<true>;

  constexpr SegmentedDeque() noexcept(
      Options.segment_reservation == 0
      && std::is_nothrow_default_constructible_v<Source> && std::is_nothrow_default_constructible_v<Directory>)
  requires(std::default_initializable<Source> && std::default_initializable<Directory>) {
    InitializeDirectory();
  }

  template<typename SourceArg>
  requires(
      std::same_as<std::remove_cvref_t<SourceArg>, Source> && std::is_constructible_v<Source, SourceArg &&>
      && std::default_initializable<Directory>)
  constexpr explicit SegmentedDeque(SourceArg&& source) noexcept(
      Options.segment_reservation == 0
      && std::is_nothrow_constructible_v<Source, SourceArg&&> && std::is_nothrow_default_constructible_v<Directory>)
      : source_(std::forward<SourceArg>(source)) {
    InitializeDirectory();
  }

  constexpr explicit SegmentedDeque(std::allocator_arg_t /*unused*/, const DirectoryAllocator& directory_allocator) noexcept(
      Options.segment_reservation == 0 && std::is_nothrow_default_constructible_v<Source>
      && std::is_nothrow_constructible_v<SegmentPointerAllocator, const DirectoryAllocator&>)
  requires(
      std::default_initializable<Source> && std::constructible_from<SegmentPointerAllocator, const DirectoryAllocator&>)
      : segments_(SegmentPointerAllocator(directory_allocator)) {
    InitializeDirectory();
  }

  template<typename SourceArg>
  requires(std::same_as<std::remove_cvref_t<SourceArg>, Source> && std::is_constructible_v<Source, SourceArg &&>
           && std::constructible_from<SegmentPointerAllocator, const DirectoryAllocator&>)
  constexpr SegmentedDeque(std::allocator_arg_t /*unused*/, const DirectoryAllocator& directory_allocator, SourceArg&& source) noexcept(
      Options.segment_reservation == 0 && std::is_nothrow_constructible_v<Source, SourceArg&&>
      && std::is_nothrow_constructible_v<SegmentPointerAllocator, const DirectoryAllocator&>)
      : source_(std::forward<SourceArg>(source)), segments_(SegmentPointerAllocator(directory_allocator)) {
    InitializeDirectory();
  }

  template<std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires(std::default_initializable<Source> && std::constructible_from<T, std::iter_reference_t<Iterator>>)
  constexpr SegmentedDeque(Iterator first, Sentinel last) {
    InitializeDirectory();
#if __cpp_exceptions
    try {
#endif
      AppendIteratorRange(first, last);
#if __cpp_exceptions
    } catch (...) {
      release();
      throw;
    }
#endif
  }

  template<std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel, typename SourceArg>
  requires(
      std::constructible_from<T, std::iter_reference_t<Iterator>>
      && std::same_as<std::remove_cvref_t<SourceArg>, Source> && std::is_constructible_v<Source, SourceArg &&>)
  constexpr SegmentedDeque(Iterator first, Sentinel last, SourceArg&& source)
      : source_(std::forward<SourceArg>(source)) {
    InitializeDirectory();
#if __cpp_exceptions
    try {
#endif
      AppendIteratorRange(first, last);
#if __cpp_exceptions
    } catch (...) {
      release();
      throw;
    }
#endif
  }

  template<std::ranges::input_range Range>
  requires(std::default_initializable<Source> && std::constructible_from<T, std::ranges::range_reference_t<Range>>)
  constexpr SegmentedDeque(std::from_range_t /*from_range*/, Range&& range) {
    InitializeDirectory();
#if __cpp_exceptions
    try {
#endif
      append_range(std::forward<Range>(range));
#if __cpp_exceptions
    } catch (...) {
      release();
      throw;
    }
#endif
  }

  template<std::ranges::input_range Range, typename SourceArg>
  requires(
      std::constructible_from<T, std::ranges::range_reference_t<Range>>
      && std::same_as<std::remove_cvref_t<SourceArg>, Source> && std::is_constructible_v<Source, SourceArg &&>)
  constexpr SegmentedDeque(std::from_range_t /*from_range*/, Range&& range, SourceArg&& source)
      : source_(std::forward<SourceArg>(source)) {
    InitializeDirectory();
#if __cpp_exceptions
    try {
#endif
      append_range(std::forward<Range>(range));
#if __cpp_exceptions
    } catch (...) {
      release();
      throw;
    }
#endif
  }

  template<std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires(
      std::default_initializable<Source> && std::constructible_from<T, std::iter_reference_t<Iterator>>
      && std::constructible_from<SegmentPointerAllocator, const DirectoryAllocator&>)
  constexpr SegmentedDeque(
      std::allocator_arg_t /*unused*/,
      const DirectoryAllocator& directory_allocator,
      Iterator first,
      Sentinel last)
      : segments_(SegmentPointerAllocator(directory_allocator)) {
    InitializeDirectory();
#if __cpp_exceptions
    try {
#endif
      AppendIteratorRange(first, last);
#if __cpp_exceptions
    } catch (...) {
      release();
      throw;
    }
#endif
  }

  template<std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel, typename SourceArg>
  requires(std::constructible_from<T, std::iter_reference_t<Iterator>>
           && std::same_as<std::remove_cvref_t<SourceArg>, Source> && std::is_constructible_v<Source, SourceArg &&>
           && std::constructible_from<SegmentPointerAllocator, const DirectoryAllocator&>)
  constexpr SegmentedDeque(
      std::allocator_arg_t /*unused*/,
      const DirectoryAllocator& directory_allocator,
      Iterator first,
      Sentinel last,
      SourceArg&& source)
      : source_(std::forward<SourceArg>(source)), segments_(SegmentPointerAllocator(directory_allocator)) {
    InitializeDirectory();
#if __cpp_exceptions
    try {
#endif
      AppendIteratorRange(first, last);
#if __cpp_exceptions
    } catch (...) {
      release();
      throw;
    }
#endif
  }

  template<std::ranges::input_range Range>
  requires(
      std::default_initializable<Source> && std::constructible_from<T, std::ranges::range_reference_t<Range>>
      && std::constructible_from<SegmentPointerAllocator, const DirectoryAllocator&>)
  constexpr SegmentedDeque(
      std::allocator_arg_t /*unused*/,
      const DirectoryAllocator& directory_allocator,
      std::from_range_t /*from_range*/,
      Range&& range)
      : segments_(SegmentPointerAllocator(directory_allocator)) {
    InitializeDirectory();
#if __cpp_exceptions
    try {
#endif
      append_range(std::forward<Range>(range));
#if __cpp_exceptions
    } catch (...) {
      release();
      throw;
    }
#endif
  }

  template<std::ranges::input_range Range, typename SourceArg>
  requires(std::constructible_from<T, std::ranges::range_reference_t<Range>>
           && std::same_as<std::remove_cvref_t<SourceArg>, Source> && std::is_constructible_v<Source, SourceArg &&>
           && std::constructible_from<SegmentPointerAllocator, const DirectoryAllocator&>)
  constexpr SegmentedDeque(
      std::allocator_arg_t /*unused*/,
      const DirectoryAllocator& directory_allocator,
      std::from_range_t /*from_range*/,
      Range&& range,
      SourceArg&& source)
      : source_(std::forward<SourceArg>(source)), segments_(SegmentPointerAllocator(directory_allocator)) {
    InitializeDirectory();
#if __cpp_exceptions
    try {
#endif
      append_range(std::forward<Range>(range));
#if __cpp_exceptions
    } catch (...) {
      release();
      throw;
    }
#endif
  }

  constexpr SegmentedDeque(const SegmentedDeque& other)
  requires(std::constructible_from<T, const T&> && mbo::memory::CopyableBlockSource<Source>)
      : SegmentedDeque(
            CopyWithAllocatorTag{},
            other,
            SegmentPointerAllocator(
                DirectoryAllocatorTraits::select_on_container_copy_construction(
                    DirectoryAllocator(other.segments_.get_allocator())))) {}

  constexpr SegmentedDeque(
      std::allocator_arg_t /*unused*/,
      const DirectoryAllocator& directory_allocator,
      const SegmentedDeque& other)
  requires(
      std::constructible_from<T, const T&> && mbo::memory::CopyableBlockSource<Source>
      && std::constructible_from<SegmentPointerAllocator, const DirectoryAllocator&>)
      : SegmentedDeque(CopyWithAllocatorTag{}, other, SegmentPointerAllocator(directory_allocator)) {}

  constexpr SegmentedDeque& operator=(const SegmentedDeque& other)
  requires(
      std::constructible_from<T, const T&> && mbo::memory::CopyableBlockSource<Source>
      && !DirectoryAllocatorTraits::propagate_on_container_copy_assignment::value
      && std::is_nothrow_swappable_v<Source> && std::copy_constructible<SegmentPointerAllocator>) {
    if (this != &other) {
      SegmentedDeque copy(CopyWithAllocatorTag{}, other, segments_.get_allocator());
      swap(copy);
    }
    return *this;
  }

  constexpr SegmentedDeque(SegmentedDeque&& other) noexcept
  requires(std::is_nothrow_move_constructible_v<Source> && std::is_nothrow_move_constructible_v<Directory>)
      : source_(std::move(other.source_)),
        segments_(std::move(other.segments_)),
        size_(other.size_),
        capacity_(other.capacity_),
        origin_(other.origin_),
        spare_(other.spare_) {
    other.size_ = 0;
    other.capacity_ = 0;
    other.origin_ = 0;
    other.spare_ = nullptr;
    other.segments_.clear();
  }

  constexpr SegmentedDeque& operator=(SegmentedDeque&& other) noexcept(
      std::is_nothrow_move_assignable_v<Source> && std::is_nothrow_move_assignable_v<Directory>)
  requires(
      std::is_nothrow_move_assignable_v<Source> && std::is_move_assignable_v<Directory>
      && (!kDirectoryMoveAssignmentAlwaysSafe || std::is_nothrow_move_assignable_v<Directory>)) {
    if (this != &other) {
      if (DirectoryAllocatorsAllowMoveTransfer(other)) {
        release();
        segments_ = std::move(other.segments_);
        source_ = std::move(other.source_);
      } else {
        Directory replacement(other.segments_, segments_.get_allocator());
        release();
        segments_.swap(replacement);
        other.segments_.clear();
        source_ = std::move(other.source_);
      }
      size_ = other.size_;
      capacity_ = other.capacity_;
      origin_ = other.origin_;
      spare_ = other.spare_;
      other.size_ = 0;
      other.capacity_ = 0;
      other.origin_ = 0;
      other.spare_ = nullptr;
      other.segments_.clear();
    }
    return *this;
  }

  constexpr ~SegmentedDeque() { release(); }

  constexpr void swap(SegmentedDeque& other) noexcept(
      std::is_nothrow_swappable_v<Source> && kDirectorySwapAlwaysSafe && noexcept(segments_.swap(other.segments_)))
  requires(std::is_nothrow_swappable_v<Source> && std::copy_constructible<SegmentPointerAllocator>) {
    using std::swap;
    if (DirectoryAllocatorsAllowSwap(other)) {
      swap(source_, other.source_);
      segments_.swap(other.segments_);
    } else {
      Directory this_directory(other.segments_, segments_.get_allocator());
      Directory other_directory(segments_, other.segments_.get_allocator());
      swap(source_, other.source_);
      segments_.swap(this_directory);
      other.segments_.swap(other_directory);
    }
    swap(size_, other.size_);
    swap(capacity_, other.capacity_);
    swap(origin_, other.origin_);
    swap(spare_, other.spare_);
  }

  friend constexpr void swap(SegmentedDeque& lhs, SegmentedDeque& rhs) noexcept(noexcept(lhs.swap(rhs)))
  requires requires { lhs.swap(rhs); } {
    lhs.swap(rhs);
  }

  constexpr bool empty() const noexcept { return size_ == 0; }

  constexpr size_type size() const noexcept { return size_; }

  constexpr size_type capacity() const noexcept { return capacity_; }

  constexpr size_type segment_count() const noexcept { return capacity_ / Options.segment_size; }

  constexpr allocator_type get_allocator() const
      noexcept(std::is_nothrow_constructible_v<DirectoryAllocator, SegmentPointerAllocator>)
  requires std::constructible_from<DirectoryAllocator, SegmentPointerAllocator> {
    return DirectoryAllocator(segments_.get_allocator());
  }

  constexpr size_type bytes_reserved() const noexcept {
    size_type result = 0;
    for (const Segment* segment : segments_) {
      result += segment->block.size;
    }
    for (const Segment* segment = spare_; segment != nullptr; segment = segment->next_spare) {
      result += segment->block.size;
    }
    return result;
  }

  constexpr size_type front_capacity() const noexcept {
    return empty() ? capacity_ : SpareCapacity() + (origin_ & kSegmentMask);
  }

  constexpr size_type back_capacity() const noexcept {
    return empty() ? capacity_ : SpareCapacity() + kSegmentMask - ((origin_ + size_ - 1) & kSegmentMask);
  }

  constexpr reference operator[](size_type pos) noexcept { return ElementAt(pos); }

  constexpr const_reference operator[](size_type pos) const noexcept { return ElementAt(pos); }

  constexpr reference at(size_type pos) noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(pos < size_, "SegmentedDeque index is out of range");
    return (*this)[pos];
  }

  constexpr const_reference at(size_type pos) const noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(pos < size_, "SegmentedDeque index is out of range");
    return (*this)[pos];
  }

  constexpr reference front() noexcept(!kRequireThrows) { return at(0); }

  constexpr const_reference front() const noexcept(!kRequireThrows) { return at(0); }

  constexpr reference back() noexcept(!kRequireThrows) { return at(size_ - 1); }

  constexpr const_reference back() const noexcept(!kRequireThrows) { return at(size_ - 1); }

  constexpr iterator begin() noexcept { return iterator(this, origin_); }

  constexpr const_iterator begin() const noexcept { return const_iterator(this, origin_); }

  constexpr const_iterator cbegin() const noexcept { return begin(); }

  constexpr iterator end() noexcept { return iterator(this, origin_ + size_); }

  constexpr const_iterator end() const noexcept { return const_iterator(this, origin_ + size_); }

  constexpr const_iterator cend() const noexcept { return end(); }

  constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }

  constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }

  constexpr reverse_iterator rend() noexcept { return reverse_iterator(begin()); }

  constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

  template<typename... Args>
  requires std::constructible_from<T, Args...>
  constexpr reference emplace_back(Args&&... args) {
    auto result = Emplace<false, true>(std::forward<Args>(args)...);
    MBO_CONFIG_REQUIRE(result.has_value(), "SegmentedDeque back allocation failed");
    return result->get();
  }

  template<typename... Args>
  requires std::constructible_from<T, Args...>
  constexpr reference emplace_front(Args&&... args) {
    auto result = Emplace<true, true>(std::forward<Args>(args)...);
    MBO_CONFIG_REQUIRE(result.has_value(), "SegmentedDeque front allocation failed");
    return result->get();
  }

  template<typename... Args>
  requires(std::constructible_from<T, Args...> && Source::supports_recoverable_failure)
  constexpr std::optional<std::reference_wrapper<T>> try_emplace_back(Args&&... args) {
    return Emplace<false, true>(std::forward<Args>(args)...);
  }

  template<typename... Args>
  requires(std::constructible_from<T, Args...> && Source::supports_recoverable_failure)
  constexpr std::optional<std::reference_wrapper<T>> try_emplace_front(Args&&... args) {
    return Emplace<true, true>(std::forward<Args>(args)...);
  }

  template<typename... Args>
  requires std::constructible_from<T, Args...>
  constexpr reference unchecked_emplace_back(Args&&... args) {
    MBO_CONFIG_REQUIRE(back_capacity() != 0, "SegmentedDeque unchecked append requires back capacity");
    // Acquire=false cannot return nullopt; the capacity precondition supplies any needed segment.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return Emplace<false, false>(std::forward<Args>(args)...).value().get();
  }

  template<typename... Args>
  requires std::constructible_from<T, Args...>
  constexpr reference unchecked_emplace_front(Args&&... args) {
    MBO_CONFIG_REQUIRE(front_capacity() != 0, "SegmentedDeque unchecked prepend requires front capacity");
    // Acquire=false cannot return nullopt; the capacity precondition supplies any needed segment.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return Emplace<true, false>(std::forward<Args>(args)...).value().get();
  }

  constexpr reference push_front(const T& value)
  requires(std::constructible_from<T, const T&>) {
    return emplace_front(value);
  }

  constexpr reference push_front(T&& value)
  requires(std::constructible_from<T, T &&>) {
    return emplace_front(std::move(value));
  }

  constexpr std::optional<std::reference_wrapper<T>> try_push_front(const T& value)
  requires(std::constructible_from<T, const T&> && Source::supports_recoverable_failure) {
    return try_emplace_front(value);
  }

  constexpr std::optional<std::reference_wrapper<T>> try_push_front(T&& value)
  requires(std::constructible_from<T, T &&> && Source::supports_recoverable_failure) {
    return try_emplace_front(std::move(value));
  }

  constexpr reference unchecked_push_front(const T& value)
  requires(std::constructible_from<T, const T&>) {
    return unchecked_emplace_front(value);
  }

  constexpr reference unchecked_push_front(T&& value)
  requires(std::constructible_from<T, T &&>) {
    return unchecked_emplace_front(std::move(value));
  }

  constexpr reference push_back(const T& value)
  requires(std::constructible_from<T, const T&>) {
    return emplace_back(value);
  }

  constexpr reference push_back(T&& value)
  requires(std::constructible_from<T, T &&>) {
    return emplace_back(std::move(value));
  }

  constexpr std::optional<std::reference_wrapper<T>> try_push_back(const T& value)
  requires(std::constructible_from<T, const T&> && Source::supports_recoverable_failure) {
    return try_emplace_back(value);
  }

  constexpr std::optional<std::reference_wrapper<T>> try_push_back(T&& value)
  requires(std::constructible_from<T, T &&> && Source::supports_recoverable_failure) {
    return try_emplace_back(std::move(value));
  }

  constexpr reference unchecked_push_back(const T& value)
  requires(std::constructible_from<T, const T&>) {
    return unchecked_emplace_back(value);
  }

  constexpr reference unchecked_push_back(T&& value)
  requires(std::constructible_from<T, T &&>) {
    return unchecked_emplace_back(std::move(value));
  }

  template<std::ranges::input_range Range>
  requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
  constexpr void append_range(Range&& range) {
    auto&& values = std::forward<Range>(range);
    AppendIteratorRange(std::ranges::begin(values), std::ranges::end(values));
  }

  template<std::ranges::input_range Range>
  requires(
      std::constructible_from<T, std::ranges::range_reference_t<Range>>
      && (std::ranges::forward_range<Range> || std::ranges::sized_range<Range> || std::constructible_from<T, T &&>))
  constexpr void prepend_range(Range&& range) {
    auto&& values = std::forward<Range>(range);
    if constexpr (std::ranges::forward_range<Range> || std::ranges::sized_range<Range>) {
      const auto count = std::ranges::distance(values);
      MBO_CONFIG_REQUIRE(std::in_range<size_type>(count), "SegmentedDeque prepend range size is invalid");
      PrependKnownRange(std::ranges::begin(values), static_cast<size_type>(count));
    } else {
      // A single-pass range has no known final prefix origin. Stage only this case, using the
      // configured allocator; forward and sized ranges construct directly, including immovable T.
      using ElementAllocator = DirectoryAllocatorTraits::template rebind_alloc<T>;
      std::vector<T, ElementAllocator> staged{ElementAllocator(get_allocator())};
      for (auto&& value : values) {
        staged.emplace_back(std::forward<decltype(value)>(value));
      }
      PrependKnownRange(std::make_move_iterator(staged.begin()), staged.size());
    }
  }

  constexpr void reserve(size_type requested) {
    MBO_CONFIG_REQUIRE(requested <= MaxCapacity(), "SegmentedDeque reserve exceeds maximum capacity");
    const size_type original_capacity = capacity_;
#if __cpp_exceptions
    try {
#endif
      while (capacity_ < requested) {
        if (!TryAddSpare()) {
          trim_capacity(original_capacity);
          MBO_CONFIG_REQUIRE(false, "SegmentedDeque reserve allocation failed");
        }
      }
#if __cpp_exceptions
    } catch (...) {
      trim_capacity(original_capacity);
      throw;
    }
#endif
  }

  constexpr void reserve_front(size_type additional) { ReserveDirectional<true>(additional); }

  constexpr void reserve_back(size_type additional) { ReserveDirectional<false>(additional); }

  constexpr void resize(size_type requested)
  requires std::default_initializable<T> {
    ResizeWith(requested, [this] { unchecked_emplace_back(); });
  }

  constexpr void resize(size_type requested, const T& value)
  requires std::constructible_from<T, const T&> {
    ResizeWith(requested, [this, &value] { unchecked_emplace_back(value); });
  }

  constexpr void pop_front() noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(!empty(), "SegmentedDeque Cannot pop an empty front");
    const size_type coordinate = origin_;
    std::destroy_at(std::addressof(AtCoordinate(coordinate)));
    ++origin_;
    --size_;
    if (empty() || (origin_ & kSegmentMask) == 0) {
      ReturnSpare(segments_.front());
      segments_.pop_front();
    }
    if (empty()) {
      origin_ = 0;
    }
  }

  constexpr void pop_back() noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(!empty(), "SegmentedDeque Cannot pop an empty back");
    const size_type coordinate = origin_ + size_ - 1;
    std::destroy_at(std::addressof(AtCoordinate(coordinate)));
    --size_;
    if (empty() || (coordinate & kSegmentMask) == 0) {
      ReturnSpare(segments_.back());
      segments_.pop_back();
    }
    if (empty()) {
      origin_ = 0;
    }
  }

  constexpr T pop_front_value() noexcept(!kRequireThrows)
  requires std::is_nothrow_move_constructible_v<T> {
    T result(std::move(front()));
    pop_front();
    return result;
  }

  constexpr T pop_back_value() noexcept(!kRequireThrows)
  requires std::is_nothrow_move_constructible_v<T> {
    T result(std::move(back()));
    pop_back();
    return result;
  }

  constexpr void clear() noexcept { DestroySuffix(0); }

  constexpr void trim_capacity() noexcept { trim_capacity(0); }

  constexpr void trim_capacity(size_type requested) noexcept {
    while (spare_ != nullptr && capacity_ - Options.segment_size >= requested) {
      ReleaseSpare();
    }
  }

  constexpr void release() noexcept {
    clear();
    trim_capacity();
  }

  constexpr segment_range segments() noexcept { return segment_range(this, LiveSegmentCount()); }

  constexpr const_segment_range segments() const noexcept { return const_segment_range(this, LiveSegmentCount()); }

 private:
  constexpr bool DirectoryAllocatorsAllowMoveTransfer(const SegmentedDeque& other) const noexcept {
    if constexpr (kDirectoryMoveAssignmentAlwaysSafe) {
      return true;
    } else {
      return segments_.get_allocator() == other.segments_.get_allocator();
    }
  }

  constexpr bool DirectoryAllocatorsAllowSwap(const SegmentedDeque& other) const noexcept {
    if constexpr (kDirectorySwapAlwaysSafe) {
      return true;
    } else {
      return segments_.get_allocator() == other.segments_.get_allocator();
    }
  }

  template<std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires std::constructible_from<T, std::iter_reference_t<Iterator>>
  constexpr void AppendIteratorRange(Iterator first, Sentinel last) {
#if __cpp_exceptions
    const size_type original_size = size_;
    const size_type original_capacity = capacity_;
    try {
#endif
      if constexpr (std::sized_sentinel_for<Sentinel, Iterator>) {
        const auto count = last - first;
        MBO_CONFIG_REQUIRE(std::in_range<size_type>(count), "SegmentedDeque range size is invalid");
        reserve_back(static_cast<size_type>(count));
      }
      for (; first != last; ++first) {
        emplace_back(*first);
      }
#if __cpp_exceptions
    } catch (...) {
      DestroySuffix(original_size);
      trim_capacity(original_capacity);
      throw;
    }
#endif
  }

  template<typename Iterator>
  constexpr void PrependKnownRange(Iterator first, size_type count) {
    if (count == 0) {
      return;
    }
#if __cpp_exceptions
    const size_type original_capacity = capacity_;
    const size_type original_origin = origin_;
#endif
    reserve_front(count);
    const size_type start = origin_ - count;
    const size_type required_segments = (((start & kSegmentMask) + size_ + count - 1) >> kSegmentShift) + 1;
    const size_type new_segments = required_segments - segments_.size();
    size_type constructed = 0;
#if __cpp_exceptions
    size_type attached = 0;
    try {
#endif
      for (size_type index = 0; index < new_segments; ++index) {
        // Publish the pointer before removing it from the spare list: a throwing
        // allocator construction must leave the spare owned by the deque.
        segments_.push_front(spare_);
        TakeSpare();
#if __cpp_exceptions
        ++attached;
#endif
      }
      // Existing element coordinates still identify the same objects, including
      // when the input range uses this deque's own iterators.
      origin_ = start;
      for (; constructed < count; ++constructed, ++first) {
        std::construct_at(std::addressof(AtCoordinate(start + constructed)), *first);
      }
#if __cpp_exceptions
    } catch (...) {
      for (size_type index = 0; index < constructed; ++index) {
        std::destroy_at(std::addressof(AtCoordinate(start + index)));
      }
      while (attached != 0) {
        ReturnSpare(segments_.front());
        segments_.pop_front();
        --attached;
      }
      origin_ = original_origin;
      trim_capacity(original_capacity);
      throw;
    }
#endif
    size_ += count;
  }

  template<typename Construct>
  constexpr void ResizeWith(size_type requested, Construct construct) {
    if (requested <= size_) {
      DestroySuffix(requested);
      return;
    }
#if __cpp_exceptions
    const size_type original_size = size_;
    const size_type original_capacity = capacity_;
    try {
#endif
      reserve_back(requested - size_);
      while (size_ < requested) {
        construct();
      }
#if __cpp_exceptions
    } catch (...) {
      DestroySuffix(original_size);
      trim_capacity(original_capacity);
      throw;
    }
#endif
  }

  static constexpr size_type MaxSegmentCount() noexcept {
    if constexpr (kFiniteDirectory) {
      return Options.segment_capacity;
    } else {
      // The experimental directory has a power-of-two slot count. This private limit keeps
      // both the directory allocation and all iterator distances representable.
      return std::bit_floor(container_internal::SegmentedRepresentationCapacityLimit<T>() / Options.segment_size);
    }
  }

  static constexpr size_type MaxCapacity() noexcept { return MaxSegmentCount() * Options.segment_size; }

  constexpr size_type LiveSegmentCount() const noexcept { return segments_.size(); }

  constexpr size_type SpareCapacity() const noexcept { return capacity_ - (LiveSegmentCount() * Options.segment_size); }

  constexpr reference AtCoordinate(size_type coordinate) noexcept { return ElementAt(coordinate - origin_); }

  constexpr const_reference AtCoordinate(size_type coordinate) const noexcept {
    return ElementAt(coordinate - origin_);
  }

  constexpr reference ElementAt(size_type pos) noexcept {
    const size_type offset = (origin_ & kSegmentMask) + pos;
    return (*segments_[offset >> kSegmentShift])[offset & kSegmentMask];
  }

  constexpr const_reference ElementAt(size_type pos) const noexcept {
    const size_type offset = (origin_ & kSegmentMask) + pos;
    return (*segments_[offset >> kSegmentShift])[offset & kSegmentMask];
  }

  template<bool IsConst>
  constexpr SegmentView<IsConst> MakeSegmentView(size_type index) const noexcept {
    const size_type first = index == 0 ? origin_ & kSegmentMask : 0;
    const size_type remaining = size_ - (index == 0 ? 0 : (index * Options.segment_size) - (origin_ & kSegmentMask));
    const size_type available = Options.segment_size - first;
    return SegmentView<IsConst>(segments_[index], first, remaining < available ? remaining : available);
  }

  constexpr void InitializeDirectory() { segments_.reserve(Options.segment_reservation); }

  template<bool Front>
  constexpr void ReserveDirectional(size_type additional) {
    MBO_CONFIG_REQUIRE(
        additional <= MaxCapacity() - size_, "SegmentedDeque directional reserve exceeds maximum capacity");
    const size_type original_capacity = capacity_;
#if __cpp_exceptions
    try {
#endif
      while ((Front ? front_capacity() : back_capacity()) < additional) {
        if (!TryAddSpare()) {
          trim_capacity(original_capacity);
          MBO_CONFIG_REQUIRE(false, "SegmentedDeque directional reserve allocation failed");
        }
      }
#if __cpp_exceptions
    } catch (...) {
      trim_capacity(original_capacity);
      throw;
    }
#endif
  }

  constexpr bool TryAddSpare() {
    if (segment_count() >= MaxSegmentCount()) {
      return false;
    }
    segments_.reserve(segment_count() + 1);
    Segment* const segment = [this]() constexpr -> Segment* {  // NOLINT(misc-const-correctness)
      if consteval {
        if constexpr (std::same_as<Source, mbo::memory::NewDeleteBlockSource>) {
          std::allocator<Segment> allocator;
          Segment* const result = allocator.allocate(1);
          return std::construct_at(
              result,
              mbo::memory::MemoryBlock{.data = nullptr, .size = sizeof(Segment), .alignment = alignof(Segment)});
        } else {
          return nullptr;
        }
      } else {
        const auto block = source_.TryAcquire(sizeof(Segment), alignof(Segment));
        if (!block || block->data == nullptr || block->size < sizeof(Segment) || block->alignment < alignof(Segment)
            || std::bit_cast<std::uintptr_t>(block->data) % alignof(Segment) != 0) {
          if (block) {
            source_.Release(*block);
          }
          return nullptr;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): allocator-style raw storage.
        return std::construct_at(reinterpret_cast<Segment*>(block->data), *block);
      }
    }();
    if (segment == nullptr) {
      return false;
    }
    ReturnSpare(segment);
    capacity_ += Options.segment_size;
    return true;
  }

  constexpr Segment* TakeSpare() noexcept {
    Segment* const segment = spare_;
    spare_ = segment->next_spare;
    segment->next_spare = nullptr;
    return segment;
  }

  constexpr void ReturnSpare(Segment* segment) noexcept {
    segment->next_spare = spare_;
    spare_ = segment;
  }

  template<bool Front, bool Acquire, typename... Args>
  constexpr std::optional<std::reference_wrapper<T>> Emplace(Args&&... args) {
    const bool needs_segment =
        empty() || (Front ? (origin_ & kSegmentMask) == 0 : ((origin_ + size_) & kSegmentMask) == 0);
    const bool acquired = Acquire && needs_segment && spare_ == nullptr;
    if (acquired && !TryAddSpare()) {
      return std::nullopt;
    }
    const size_type coordinate = Front ? origin_ - 1 : origin_ + size_;
    Segment* const segment = needs_segment ? TakeSpare() : (Front ? segments_.front() : segments_.back());
    T* result = nullptr;
#if __cpp_exceptions
    try {
#endif
      result = std::construct_at(std::addressof((*segment)[coordinate & kSegmentMask]), std::forward<Args>(args)...);
      if (needs_segment) {
        if constexpr (Front) {
          segments_.push_front(segment);
        } else {
          segments_.push_back(segment);
        }
      }
#if __cpp_exceptions
    } catch (...) {
      if (result != nullptr) {
        std::destroy_at(result);
      }
      if (needs_segment) {
        ReturnSpare(segment);
      }
      if (acquired) {
        ReleaseSpare();
      }
      throw;
    }
#endif
    if constexpr (Front) {
      origin_ = coordinate;
    }
    ++size_;
    return std::ref(*result);
  }

  constexpr void ReleaseSpare() noexcept {
    Segment* const segment = TakeSpare();
    const mbo::memory::MemoryBlock block = segment->block;
    capacity_ -= Options.segment_size;
    std::destroy_at(segment);
    if consteval {
      if constexpr (std::same_as<Source, mbo::memory::NewDeleteBlockSource>) {
        std::allocator<Segment> allocator;
        allocator.deallocate(segment, 1);
      }
    } else {
      source_.Release(block);
    }
  }

  constexpr void DestroySuffix(size_type requested) noexcept {
    while (size_ > requested) {
      pop_back();
    }
  }

  constexpr SegmentedDeque(
      CopyWithAllocatorTag /*unused*/,
      const SegmentedDeque& other,
      const SegmentPointerAllocator& directory_allocator)
  requires(std::constructible_from<T, const T&> && mbo::memory::CopyableBlockSource<Source>)
      : source_(other.source_.CopyForContainer()), segments_(directory_allocator) {
    InitializeDirectory();
#if __cpp_exceptions
    try {
#endif
      reserve_back(other.size());
      for (const T& value : other) {
        unchecked_emplace_back(value);
      }
#if __cpp_exceptions
    } catch (...) {
      release();
      throw;
    }
#endif
  }

  [[no_unique_address]] Source source_{};
  Directory segments_;
  std::size_t size_ = 0;
  std::size_t capacity_ = 0;
  std::size_t origin_ = 0;
  Segment* spare_ = nullptr;
};

}  // namespace mbo::container

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// NOLINTEND(readability-identifier-naming)

#endif  // MBO_CONTAINER_SEGMENTED_DEQUE_H_
