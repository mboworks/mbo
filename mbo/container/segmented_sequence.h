// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef MBO_CONTAINER_SEGMENTED_SEQUENCE_H_
#define MBO_CONTAINER_SEGMENTED_SEQUENCE_H_

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
#include "mbo/memory/block_source.h"

namespace mbo::container {

// NOLINTBEGIN(readability-identifier-naming): SegmentedSequence models the STL container interface.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access):
// bounded internal storage and checked logical positions.

struct SegmentedSequenceOptions final {
  std::size_t segment_size = 256;
  // Maximum number of segment slots. SIZE_MAX selects unbounded directory growth.
  std::size_t segment_capacity = std::numeric_limits<std::size_t>::max();
  // Initial number of segment-directory slots to reserve.
  std::size_t segment_reservation = 1;

  constexpr bool IsValid() const noexcept {
    return std::has_single_bit(segment_size)
           && (segment_capacity == std::numeric_limits<std::size_t>::max() || std::has_single_bit(segment_capacity))
           && (segment_reservation == 0 || std::has_single_bit(segment_reservation))
           && (segment_capacity == std::numeric_limits<std::size_t>::max() || segment_reservation <= segment_capacity);
  }
};

template<SegmentedSequenceOptions Options>
concept ValidSegmentedSequenceOptions = Options.IsValid();

template<typename T>
concept SegmentedSequenceElement = std::is_object_v<T> && !std::is_array_v<T> && std::same_as<T, std::remove_cv_t<T>>
                                   && requires { sizeof(T); } && std::destructible<T>;

namespace container_internal {

template<SegmentedSequenceElement T>
constexpr std::size_t SegmentedSequenceRepresentationCapacityLimit() noexcept {
  constexpr auto kDifferenceLimit = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
  constexpr std::size_t kObjectLimit = std::numeric_limits<std::size_t>::max() / sizeof(T);
  return kDifferenceLimit < kObjectLimit ? kDifferenceLimit : kObjectLimit;
}

}  // namespace container_internal

template<typename T, SegmentedSequenceOptions Options>
concept RepresentableSegmentedSequenceOptions =
    SegmentedSequenceElement<T> && ValidSegmentedSequenceOptions<Options>
    && Options.segment_size
           <= (static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) - sizeof(mbo::memory::MemoryBlock)
               - (2 * sizeof(std::size_t)) - (alignof(T) - 1)
               - ((alignof(T) < alignof(mbo::memory::MemoryBlock) ? alignof(mbo::memory::MemoryBlock) : alignof(T))
                  - 1))
                  / sizeof(T)
    && (Options.segment_capacity == std::numeric_limits<std::size_t>::max()
        || Options.segment_capacity
               <= container_internal::SegmentedSequenceRepresentationCapacityLimit<T>() / Options.segment_size)
    && Options.segment_reservation
           <= container_internal::SegmentedSequenceRepresentationCapacityLimit<T>() / Options.segment_size;

template<
    SegmentedSequenceElement T,
    SegmentedSequenceOptions Options = {},
    mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource,
    typename DirectoryAllocator = std::allocator<std::byte>>
requires RepresentableSegmentedSequenceOptions<T, Options>
class SegmentedSequence final {
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
    constexpr explicit Segment(mbo::memory::MemoryBlock block) noexcept : block(block) {}

    Segment(const Segment&) = delete;
    Segment& operator=(const Segment&) = delete;
    Segment(Segment&&) = delete;
    Segment& operator=(Segment&&) = delete;
    constexpr ~Segment() = default;

    mbo::memory::MemoryBlock block{};
    std::size_t size = 0;
    std::array<Slot, Options.segment_size> slots;
  };

  using SegmentPointerAllocator = std::allocator_traits<DirectoryAllocator>::template rebind_alloc<Segment*>;
  using DirectoryAllocatorTraits = std::allocator_traits<DirectoryAllocator>;
  using SegmentPointerAllocatorTraits = std::allocator_traits<SegmentPointerAllocator>;
  using Directory = std::vector<Segment*, SegmentPointerAllocator>;
  static constexpr bool kDirectorySwapAlwaysSafe = SegmentPointerAllocatorTraits::propagate_on_container_swap::value
                                                   || SegmentPointerAllocatorTraits::is_always_equal::value;
  static constexpr bool kDirectoryMoveAssignmentAlwaysSafe =
      SegmentPointerAllocatorTraits::propagate_on_container_move_assignment::value
      || SegmentPointerAllocatorTraits::is_always_equal::value;

  struct CopyWithAllocatorTag final {};

  template<bool IsConst>
  class Iterator final {
   private:
    using Owner = std::conditional_t<IsConst, const SegmentedSequence, SegmentedSequence>;

    template<bool>
    friend class Iterator;
    friend class SegmentedSequence;

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

    constexpr reference operator*() const { return (*owner_)[pos_]; }

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
      pos_ = static_cast<std::size_t>(static_cast<difference_type>(pos_) + offset);
      return *this;
    }

    constexpr Iterator& operator-=(difference_type offset) noexcept { return *this += -offset; }

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
      return static_cast<difference_type>(lhs.pos_) - static_cast<difference_type>(rhs.pos_);
    }

    friend constexpr bool operator==(const Iterator& lhs, const Iterator& rhs) noexcept(!kRequireThrows) {
      MBO_CONFIG_REQUIRE(lhs.owner_ == rhs.owner_, "Cannot compare iterators from different sequences");
      return lhs.pos_ == rhs.pos_;
    }

    friend constexpr auto operator<=>(const Iterator& lhs, const Iterator& rhs) noexcept(!kRequireThrows) {
      MBO_CONFIG_REQUIRE(lhs.owner_ == rhs.owner_, "Cannot compare iterators from different sequences");
      return lhs.pos_ <=> rhs.pos_;
    }

   private:
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

      constexpr reference operator*() const noexcept { return segment_->slots[pos_].value; }

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
        pos_ = static_cast<std::size_t>(static_cast<difference_type>(pos_) + offset);
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

    friend class SegmentedSequence;

    constexpr explicit SegmentView(SegmentType* segment) noexcept : segment_(segment) {}

   public:
    using iterator = ViewIterator;

    constexpr iterator begin() const noexcept { return iterator(segment_, 0); }

    constexpr iterator end() const noexcept { return iterator(segment_, size()); }

    constexpr decltype(auto) operator[](std::size_t pos) const noexcept { return (segment_->slots[pos].value); }

    constexpr std::size_t size() const noexcept { return segment_->size; }

    constexpr bool empty() const noexcept { return size() == 0; }

   private:
    SegmentType* segment_ = nullptr;
  };

  template<bool IsConst>
  class SegmentRange final {
   private:
    using Owner = std::conditional_t<IsConst, const SegmentedSequence, SegmentedSequence>;
    using View = SegmentView<IsConst>;

    class SegmentIterator final {
     public:
      using iterator_category = std::forward_iterator_tag;
      using iterator_concept = std::forward_iterator_tag;
      using value_type = View;
      using difference_type = std::ptrdiff_t;

      constexpr SegmentIterator() noexcept = default;

      constexpr SegmentIterator(Owner* owner, std::size_t pos) noexcept : owner_(owner), pos_(pos) {}

      constexpr value_type operator*() const noexcept { return value_type(owner_->segments_[pos_]); }

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

    friend class SegmentedSequence;

    constexpr SegmentRange(Owner* owner, std::size_t size) noexcept : owner_(owner), size_(size) {}

   public:
    constexpr SegmentIterator begin() const noexcept { return SegmentIterator(owner_, 0); }

    constexpr SegmentIterator end() const noexcept { return SegmentIterator(owner_, size_); }

    constexpr View operator[](std::size_t pos) const noexcept { return View(owner_->segments_[pos]); }

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

  constexpr SegmentedSequence() noexcept(
      Options.segment_reservation == 0
      && std::is_nothrow_default_constructible_v<Source> && std::is_nothrow_default_constructible_v<Directory>)
  requires(std::default_initializable<Source> && std::default_initializable<Directory>) {
    InitializeDirectory();
  }

  constexpr explicit SegmentedSequence(Source source) noexcept(
      Options.segment_reservation == 0
      && std::is_nothrow_move_constructible_v<Source> && std::is_nothrow_default_constructible_v<Directory>)
  requires(std::constructible_from<Source, Source &&> && std::default_initializable<Directory>)
      : source_(std::move(source)) {
    InitializeDirectory();
  }

  constexpr explicit SegmentedSequence(std::allocator_arg_t /*unused*/, const DirectoryAllocator& directory_allocator) noexcept(
      Options.segment_reservation == 0 && std::is_nothrow_default_constructible_v<Source>
      && std::is_nothrow_constructible_v<SegmentPointerAllocator, const DirectoryAllocator&>)
  requires(
      std::default_initializable<Source> && std::constructible_from<SegmentPointerAllocator, const DirectoryAllocator&>)
      : segments_(SegmentPointerAllocator(directory_allocator)) {
    InitializeDirectory();
  }

  constexpr SegmentedSequence(std::allocator_arg_t /*unused*/, const DirectoryAllocator& directory_allocator, Source source) noexcept(
      Options.segment_reservation == 0 && std::is_nothrow_move_constructible_v<Source>
      && std::is_nothrow_constructible_v<SegmentPointerAllocator, const DirectoryAllocator&>)
  requires(std::constructible_from<Source, Source &&>
           && std::constructible_from<SegmentPointerAllocator, const DirectoryAllocator&>)
      : source_(std::move(source)), segments_(SegmentPointerAllocator(directory_allocator)) {
    InitializeDirectory();
  }

  template<std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires(std::default_initializable<Source> && std::constructible_from<T, std::iter_reference_t<Iterator>>)
  constexpr SegmentedSequence(Iterator first, Sentinel last) {
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

  template<std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires(std::constructible_from<T, std::iter_reference_t<Iterator>> && std::constructible_from<Source, Source &&>)
  constexpr SegmentedSequence(Iterator first, Sentinel last, Source source) : source_(std::move(source)) {
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
  constexpr SegmentedSequence(std::from_range_t /*from_range*/, Range&& range) {
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

  template<std::ranges::input_range Range>
  requires(
      std::constructible_from<T, std::ranges::range_reference_t<Range>> && std::constructible_from<Source, Source &&>)
  constexpr SegmentedSequence(std::from_range_t /*from_range*/, Range&& range, Source source)
      : source_(std::move(source)) {
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
  constexpr SegmentedSequence(
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

  template<std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires(std::constructible_from<T, std::iter_reference_t<Iterator>> && std::constructible_from<Source, Source &&>
           && std::constructible_from<SegmentPointerAllocator, const DirectoryAllocator&>)
  constexpr SegmentedSequence(
      std::allocator_arg_t /*unused*/,
      const DirectoryAllocator& directory_allocator,
      Iterator first,
      Sentinel last,
      Source source)
      : source_(std::move(source)), segments_(SegmentPointerAllocator(directory_allocator)) {
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
  constexpr SegmentedSequence(
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

  template<std::ranges::input_range Range>
  requires(std::constructible_from<T, std::ranges::range_reference_t<Range>>
           && std::constructible_from<Source, Source &&>
           && std::constructible_from<SegmentPointerAllocator, const DirectoryAllocator&>)
  constexpr SegmentedSequence(
      std::allocator_arg_t /*unused*/,
      const DirectoryAllocator& directory_allocator,
      std::from_range_t /*from_range*/,
      Range&& range,
      Source source)
      : source_(std::move(source)), segments_(SegmentPointerAllocator(directory_allocator)) {
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

  constexpr SegmentedSequence(const SegmentedSequence& other)
  requires(std::constructible_from<T, const T&> && mbo::memory::CopyableBlockSource<Source>)
      : SegmentedSequence(
            CopyWithAllocatorTag{},
            other,
            SegmentPointerAllocator(
                DirectoryAllocatorTraits::select_on_container_copy_construction(
                    DirectoryAllocator(other.segments_.get_allocator())))) {}

  constexpr SegmentedSequence(
      std::allocator_arg_t /*unused*/,
      const DirectoryAllocator& directory_allocator,
      const SegmentedSequence& other)
  requires(
      std::constructible_from<T, const T&> && mbo::memory::CopyableBlockSource<Source>
      && std::constructible_from<SegmentPointerAllocator, const DirectoryAllocator&>)
      : SegmentedSequence(CopyWithAllocatorTag{}, other, SegmentPointerAllocator(directory_allocator)) {}

  constexpr SegmentedSequence& operator=(const SegmentedSequence& other)
  requires(
      std::constructible_from<T, const T&> && mbo::memory::CopyableBlockSource<Source>
      && !DirectoryAllocatorTraits::propagate_on_container_copy_assignment::value
      && std::is_nothrow_swappable_v<Source> && std::copy_constructible<SegmentPointerAllocator>) {
    if (this != &other) {
      SegmentedSequence copy(CopyWithAllocatorTag{}, other, segments_.get_allocator());
      swap(copy);
    }
    return *this;
  }

  constexpr SegmentedSequence(SegmentedSequence&& other) noexcept
  requires(std::is_nothrow_move_constructible_v<Source> && std::is_nothrow_move_constructible_v<Directory>)
      : source_(std::move(other.source_)),
        segments_(std::move(other.segments_)),
        size_(other.size_),
        capacity_(other.capacity_) {
    other.size_ = 0;
    other.capacity_ = 0;
    other.segments_.clear();
  }

  constexpr SegmentedSequence& operator=(SegmentedSequence&& other) noexcept(
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
        Directory replacement(segments_.get_allocator());
        replacement.reserve(
            Options.segment_reservation < other.segments_.size() ? other.segments_.size()
                                                                 : Options.segment_reservation);
        replacement.insert(replacement.end(), other.segments_.begin(), other.segments_.end());
        release();
        segments_.swap(replacement);
        other.segments_.clear();
        source_ = std::move(other.source_);
      }
      size_ = other.size_;
      capacity_ = other.capacity_;
      other.size_ = 0;
      other.capacity_ = 0;
      other.segments_.clear();
    }
    return *this;
  }

  constexpr ~SegmentedSequence() { release(); }

  constexpr void swap(SegmentedSequence& other) noexcept(
      std::is_nothrow_swappable_v<Source> && kDirectorySwapAlwaysSafe && noexcept(segments_.swap(other.segments_)))
  requires(std::is_nothrow_swappable_v<Source> && std::copy_constructible<SegmentPointerAllocator>) {
    using std::swap;
    if (DirectoryAllocatorsAllowSwap(other)) {
      swap(source_, other.source_);
      segments_.swap(other.segments_);
    } else {
      Directory this_directory(segments_.get_allocator());
      this_directory.reserve(
          Options.segment_reservation < other.segments_.size() ? other.segments_.size() : Options.segment_reservation);
      this_directory.insert(this_directory.end(), other.segments_.begin(), other.segments_.end());
      Directory other_directory(other.segments_.get_allocator());
      other_directory.reserve(
          Options.segment_reservation < segments_.size() ? segments_.size() : Options.segment_reservation);
      other_directory.insert(other_directory.end(), segments_.begin(), segments_.end());
      swap(source_, other.source_);
      segments_.swap(this_directory);
      other.segments_.swap(other_directory);
    }
    swap(size_, other.size_);
    swap(capacity_, other.capacity_);
  }

  friend constexpr void swap(SegmentedSequence& lhs, SegmentedSequence& rhs) noexcept(noexcept(lhs.swap(rhs)))
  requires requires { lhs.swap(rhs); } {
    lhs.swap(rhs);
  }

  constexpr bool empty() const noexcept { return size_ == 0; }

  constexpr size_type size() const noexcept { return size_; }

  constexpr size_type capacity() const noexcept { return capacity_; }

  constexpr size_type segment_count() const noexcept { return segments_.size(); }

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
    return result;
  }

  constexpr reference operator[](size_type pos) noexcept { return ElementAt(pos); }

  constexpr const_reference operator[](size_type pos) const noexcept { return ElementAt(pos); }

  constexpr reference at(size_type pos) noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(pos < size_, "SegmentedSequence index is out of range");
    return (*this)[pos];
  }

  constexpr const_reference at(size_type pos) const noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(pos < size_, "SegmentedSequence index is out of range");
    return (*this)[pos];
  }

  constexpr reference front() noexcept(!kRequireThrows) { return at(0); }

  constexpr const_reference front() const noexcept(!kRequireThrows) { return at(0); }

  constexpr reference back() noexcept(!kRequireThrows) { return at(size_ - 1); }

  constexpr const_reference back() const noexcept(!kRequireThrows) { return at(size_ - 1); }

  constexpr iterator begin() noexcept { return iterator(this, 0); }

  constexpr const_iterator begin() const noexcept { return const_iterator(this, 0); }

  constexpr const_iterator cbegin() const noexcept { return begin(); }

  constexpr iterator end() noexcept { return iterator(this, size_); }

  constexpr const_iterator end() const noexcept { return const_iterator(this, size_); }

  constexpr const_iterator cend() const noexcept { return end(); }

  constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }

  constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }

  constexpr reverse_iterator rend() noexcept { return reverse_iterator(begin()); }

  constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

  template<typename... Args>
  requires std::constructible_from<T, Args...>
  constexpr reference emplace_back(Args&&... args) {
#if __cpp_exceptions
    const bool added_segment = size_ == capacity_;
#endif
    if (size_ == capacity_) {
      MBO_CONFIG_REQUIRE(TryAddSegment(), "SegmentedSequence allocation failed");
    }
#if __cpp_exceptions
    try {
#endif
      return unchecked_emplace_back(std::forward<Args>(args)...);
#if __cpp_exceptions
    } catch (...) {
      if (added_segment) {
        ReleaseLastEmptySegment();
      }
      throw;
    }
#endif
  }

  template<typename... Args>
  requires std::constructible_from<T, Args...>
  constexpr std::optional<std::reference_wrapper<T>> try_emplace_back(Args&&... args)
  requires Source::supports_recoverable_failure {
#if __cpp_exceptions
    const bool added_segment = size_ == capacity_;
#endif
    if (size_ == capacity_ && !TryAddSegment()) {
      return std::nullopt;
    }
#if __cpp_exceptions
    try {
#endif
      return std::ref(unchecked_emplace_back(std::forward<Args>(args)...));
#if __cpp_exceptions
    } catch (...) {
      if (added_segment) {
        ReleaseLastEmptySegment();
      }
      throw;
    }
#endif
  }

  template<typename... Args>
  requires std::constructible_from<T, Args...>
  constexpr reference unchecked_emplace_back(Args&&... args) {
    MBO_CONFIG_REQUIRE(size_ < capacity_, "SegmentedSequence unchecked append requires reserved capacity");
    const auto [segment_index, offset] = Locate(size_);
    Segment& segment = *segments_[segment_index];
    MBO_CONFIG_REQUIRE(offset == segment.size, "SegmentedSequence segment prefix is inconsistent");
    T* const result = std::construct_at(std::addressof(segment.slots[offset].value), std::forward<Args>(args)...);
    ++segment.size;
    ++size_;
    return *result;
  }

  constexpr reference push_back(const T& value)
  requires std::constructible_from<T, const T&> {
    return emplace_back(value);
  }

  constexpr reference push_back(T&& value)
  requires std::constructible_from<T, T&&> {
    return emplace_back(std::move(value));
  }

  constexpr std::optional<std::reference_wrapper<T>> try_push_back(const T& value)
  requires Source::supports_recoverable_failure && std::constructible_from<T, const T&> {
    return try_emplace_back(value);
  }

  constexpr std::optional<std::reference_wrapper<T>> try_push_back(T&& value)
  requires Source::supports_recoverable_failure && std::constructible_from<T, T&&> {
    return try_emplace_back(std::move(value));
  }

  constexpr reference unchecked_push_back(const T& value)
  requires std::constructible_from<T, const T&> {
    return unchecked_emplace_back(value);
  }

  constexpr reference unchecked_push_back(T&& value)
  requires std::constructible_from<T, T&&> {
    return unchecked_emplace_back(std::move(value));
  }

  template<std::ranges::input_range Range>
  requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
  constexpr void append_range(Range&& range) {
#if __cpp_exceptions
    const size_type original_size = size_;
    const size_type original_segment_count = segments_.size();
    try {
#endif
      if constexpr (std::ranges::sized_range<Range>) {
        const auto count = std::ranges::size(range);
        MBO_CONFIG_REQUIRE(
            std::in_range<size_type>(count) && static_cast<size_type>(count) <= MaxCapacity() - size_,
            "SegmentedSequence append exceeds maximum capacity");
        reserve(size_ + static_cast<size_type>(count));
      }
      for (auto&& value : std::forward<Range>(range)) {
        emplace_back(std::forward<decltype(value)>(value));
      }
#if __cpp_exceptions
    } catch (...) {
      DestroySuffix(original_size);
      ReleaseSegmentsFrom(original_segment_count);
      throw;
    }
#endif
  }

  constexpr void reserve(size_type requested) {
    MBO_CONFIG_REQUIRE(requested <= MaxCapacity(), "SegmentedSequence reserve exceeds maximum capacity");
    const std::size_t original_segment_count = segments_.size();
#if __cpp_exceptions
    try {
#endif
      while (capacity_ < requested) {
        if (!TryAddSegment()) {
          ReleaseSegmentsFrom(original_segment_count);
          MBO_CONFIG_REQUIRE(false, "SegmentedSequence allocation failed");
        }
      }
#if __cpp_exceptions
    } catch (...) {
      ReleaseSegmentsFrom(original_segment_count);
      throw;
    }
#endif
  }

  constexpr void resize(size_type requested)
  requires std::default_initializable<T> {
    if (requested < size_) {
      DestroySuffix(requested);
      return;
    }
#if __cpp_exceptions
    const size_type original_size = size_;
    const size_type original_segment_count = segments_.size();
    try {
#endif
      reserve(requested);
      while (size_ != requested) {
        unchecked_emplace_back();
      }
#if __cpp_exceptions
    } catch (...) {
      DestroySuffix(original_size);
      ReleaseSegmentsFrom(original_segment_count);
      throw;
    }
#endif
  }

  constexpr void resize(size_type requested, const T& value)
  requires std::constructible_from<T, const T&> {
    if (requested < size_) {
      DestroySuffix(requested);
      return;
    }
#if __cpp_exceptions
    const size_type original_size = size_;
    const size_type original_segment_count = segments_.size();
    try {
#endif
      reserve(requested);
      while (size_ != requested) {
        unchecked_emplace_back(value);
      }
#if __cpp_exceptions
    } catch (...) {
      DestroySuffix(original_size);
      ReleaseSegmentsFrom(original_segment_count);
      throw;
    }
#endif
  }

  constexpr void pop_back() noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(!empty(), "Cannot pop from an empty SegmentedSequence");
    DestroySuffix(size_ - 1);
  }

  constexpr T pop_back_value() noexcept(!kRequireThrows)
  requires std::is_nothrow_move_constructible_v<T> {
    T result(std::move(back()));
    pop_back();
    return result;
  }

  constexpr void clear() noexcept { DestroySuffix(0); }

  constexpr void trim_capacity() noexcept {
    while (!segments_.empty() && segments_.back()->size == 0) {
      ReleaseLastEmptySegment();
    }
  }

  constexpr void trim_capacity(size_type requested) noexcept {
    const std::size_t target = requested < size_ ? size_ : requested;
    while (!segments_.empty() && segments_.back()->size == 0 && capacity_ - Options.segment_size >= target) {
      ReleaseLastEmptySegment();
    }
  }

  constexpr void release() noexcept {
    clear();
    while (!segments_.empty()) {
      ReleaseLastEmptySegment();
    }
  }

  constexpr segment_range segments() noexcept { return segment_range(this, LiveSegmentCount()); }

  constexpr const_segment_range segments() const noexcept { return const_segment_range(this, LiveSegmentCount()); }

 private:
  constexpr bool DirectoryAllocatorsAllowMoveTransfer(const SegmentedSequence& other) const noexcept {
    if constexpr (kDirectoryMoveAssignmentAlwaysSafe) {
      return true;
    } else {
      return segments_.get_allocator() == other.segments_.get_allocator();
    }
  }

  constexpr bool DirectoryAllocatorsAllowSwap(const SegmentedSequence& other) const noexcept {
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
    const size_type original_segment_count = segments_.size();
    try {
#endif
      if constexpr (std::sized_sentinel_for<Sentinel, Iterator>) {
        const auto count = last - first;
        MBO_CONFIG_REQUIRE(count >= 0, "SegmentedSequence range has negative size");
        MBO_CONFIG_REQUIRE(
            std::in_range<size_type>(count) && static_cast<size_type>(count) <= MaxCapacity() - size_,
            "SegmentedSequence append exceeds maximum capacity");
        reserve(size_ + static_cast<size_type>(count));
      }
      for (; first != last; ++first) {
        emplace_back(*first);
      }
#if __cpp_exceptions
    } catch (...) {
      DestroySuffix(original_size);
      ReleaseSegmentsFrom(original_segment_count);
      throw;
    }
#endif
  }

  static constexpr std::pair<std::size_t, std::size_t> Locate(std::size_t pos) noexcept {
    return {pos >> kSegmentShift, pos & kSegmentMask};
  }

  static constexpr std::size_t RepresentationCapacityLimit() noexcept {
    return container_internal::SegmentedSequenceRepresentationCapacityLimit<T>();
  }

  static constexpr std::size_t MaxSegmentCount() noexcept {
    if constexpr (kFiniteDirectory) {
      return Options.segment_capacity;
    } else {
      return RepresentationCapacityLimit() / Options.segment_size;
    }
  }

  static constexpr std::size_t MaxCapacity() noexcept { return MaxSegmentCount() * Options.segment_size; }

  constexpr std::size_t LiveSegmentCount() const noexcept { return empty() ? 0 : Locate(size_ - 1).first + 1; }

  constexpr reference ElementAt(std::size_t pos) noexcept {
    const auto [segment_index, offset] = Locate(pos);
    return segments_[segment_index]->slots[offset].value;
  }

  constexpr const_reference ElementAt(std::size_t pos) const noexcept {
    const auto [segment_index, offset] = Locate(pos);
    return segments_[segment_index]->slots[offset].value;
  }

  constexpr void InitializeDirectory() {
    if constexpr (Options.segment_reservation > 0) {
      segments_.reserve(Options.segment_reservation);
    }
  }

  constexpr void EnsureDirectoryCapacity(std::size_t required) {
    if (required <= segments_.capacity()) {
      return;
    }
    const std::size_t requested = [required] {
      const std::size_t next = std::bit_ceil(required);
      return next < MaxSegmentCount() ? next : MaxSegmentCount();
    }();
    // std::vector may reserve more, but every explicit request follows a power-of-two threshold.
    segments_.reserve(requested);
  }

  constexpr bool TryAddSegment() {
    if (segments_.size() >= MaxSegmentCount()) {
      return false;
    }
    EnsureDirectoryCapacity(segments_.size() + 1);
    // The directory intentionally stores mutable Segment pointers: later appends construct Slots.
    Segment* const segment = [this]() constexpr -> Segment* {  // NOLINT(misc-const-correctness)
      if consteval {
        if constexpr (std::same_as<Source, mbo::memory::NewDeleteBlockSource>) {
          std::allocator<Segment> allocator;
          Segment* const result = allocator.allocate(1);
          return std::construct_at(
              result, mbo::memory::MemoryBlock{
                          .data = nullptr,
                          .size = sizeof(Segment),
                          .alignment = alignof(Segment),
                      });
        } else {
          // A custom source's exhaustion and state are part of its semantics. Do not bypass them
          // with typed constant-evaluation storage that the source did not supply.
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
        // The source supplies raw storage. construct_at begins one complete Segment object
        // containing its header and inactive element Slots; individual T lifetimes start only on
        // append.
        return std::construct_at(
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): allocator-style storage.
            reinterpret_cast<Segment*>(block->data), *block);
      }
    }();
    if (segment == nullptr) {
      return false;
    }
#if __cpp_exceptions
    try {
#endif
      segments_.push_back(segment);
#if __cpp_exceptions
    } catch (...) {
      DestroySegmentStorage(segment);
      throw;
    }
#endif
    capacity_ += Options.segment_size;
    return true;
  }

  constexpr void ReleaseLastEmptySegment() noexcept {
    Segment* const segment = segments_.back();
    capacity_ -= Options.segment_size;
    segments_.pop_back();
    DestroySegmentStorage(segment);
  }

  constexpr void ReleaseSegmentsFrom(std::size_t first) noexcept {
    while (segments_.size() > first) {
      ReleaseLastEmptySegment();
    }
  }

  constexpr void DestroySuffix(std::size_t requested) noexcept {
    while (size_ > requested) {
      const auto [segment_index, offset] = Locate(size_ - 1);
      Segment& segment = *segments_[segment_index];
      --segment.size;
      --size_;
      std::destroy_at(std::addressof(segment.slots[offset].value));
    }
  }

  constexpr void DestroySegmentStorage(Segment* segment) noexcept {
    const mbo::memory::MemoryBlock block = segment->block;
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

  constexpr SegmentedSequence(
      CopyWithAllocatorTag /*unused*/,
      const SegmentedSequence& other,
      const SegmentPointerAllocator& directory_allocator)
  requires(std::constructible_from<T, const T&> && mbo::memory::CopyableBlockSource<Source>)
      : source_(other.source_.CopyForContainer()), segments_(directory_allocator) {
    InitializeDirectory();
#if __cpp_exceptions
    try {
#endif
      reserve(other.size());
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
};

}  // namespace mbo::container

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// NOLINTEND(readability-identifier-naming)

#endif  // MBO_CONTAINER_SEGMENTED_SEQUENCE_H_
