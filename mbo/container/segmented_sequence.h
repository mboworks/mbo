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
#include <optional>
#include <span>
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
  static constexpr std::size_t kMaxListedCapacities = 8;

  std::array<std::size_t, kMaxListedCapacities> segment_capacities = {256};
  std::size_t listed_capacities = 1;
  bool repeat_last = true;
  std::size_t maximum_size = std::numeric_limits<std::size_t>::max();

  constexpr bool IsValid() const noexcept {
    if (listed_capacities == 0 || listed_capacities > segment_capacities.size()) {
      return false;
    }
    std::size_t total = 0;
    for (std::size_t pos = 0; pos < listed_capacities; ++pos) {
      const std::size_t capacity = segment_capacities[pos];
      if (capacity == 0 || capacity > maximum_size - total) {
        return false;
      }
      total += capacity;
    }
    return maximum_size > 0;
  }
};

template<SegmentedSequenceOptions Options>
concept ValidSegmentedSequenceOptions = Options.IsValid();

template<
    typename T,
    SegmentedSequenceOptions Options = {},
    mbo::memory::BlockSource Source = mbo::memory::NewDeleteBlockSource>
requires ValidSegmentedSequenceOptions<Options>
class SegmentedSequence final {
 private:
  static constexpr bool kRequireThrows = ::mbo::config::kRequireThrows;

  struct Segment final {
    mbo::memory::MemoryBlock block;
    T* data = nullptr;
    std::size_t capacity = 0;
    std::size_t size = 0;
  };

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
  class SegmentRange final {
   private:
    using Owner = std::conditional_t<IsConst, const SegmentedSequence, SegmentedSequence>;
    using Element = std::conditional_t<IsConst, const T, T>;

    class SegmentIterator final {
     public:
      using iterator_category = std::forward_iterator_tag;
      using iterator_concept = std::forward_iterator_tag;
      using value_type = std::span<Element>;
      using difference_type = std::ptrdiff_t;

      constexpr SegmentIterator() noexcept = default;

      constexpr SegmentIterator(Owner* owner, std::size_t pos) noexcept : owner_(owner), pos_(pos) {}

      constexpr value_type operator*() const noexcept {
        const Segment& segment = owner_->segments_[pos_];
        return value_type(segment.data, segment.size);
      }

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

    constexpr std::span<Element> operator[](std::size_t pos) const noexcept {
      const Segment& segment = owner_->segments_[pos];
      return std::span<Element>(segment.data, segment.size);
    }

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
  using segment_range = SegmentRange<false>;
  using const_segment_range = SegmentRange<true>;

  constexpr SegmentedSequence() noexcept(std::is_nothrow_default_constructible_v<Source>) = default;

  constexpr explicit SegmentedSequence(Source source) noexcept(std::is_nothrow_move_constructible_v<Source>)
      : source_(std::move(source)) {}

  constexpr SegmentedSequence(const SegmentedSequence& other)
  requires(std::constructible_from<T, const T&> && mbo::memory::CopyableBlockSource<Source>)
      : source_(other.source_.CopyForContainer()) {
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

  constexpr SegmentedSequence& operator=(const SegmentedSequence& other)
  requires(
      std::constructible_from<T, const T&> && mbo::memory::CopyableBlockSource<Source>
      && std::is_nothrow_swappable_v<Source>)
  {
    if (this != &other) {
      SegmentedSequence copy(other);
      swap(copy);
    }
    return *this;
  }

  constexpr SegmentedSequence(SegmentedSequence&& other) noexcept(std::is_nothrow_move_constructible_v<Source>)
  requires std::move_constructible<Source>
      : source_(std::move(other.source_)),
        segments_(std::move(other.segments_)),
        size_(other.size_),
        capacity_(other.capacity_) {
    other.size_ = 0;
    other.capacity_ = 0;
    other.segments_.clear();
  }

  constexpr SegmentedSequence& operator=(SegmentedSequence&& other) noexcept(std::is_nothrow_move_assignable_v<Source>)
  requires std::is_move_assignable_v<Source>
  {
    if (this != &other) {
      release();
      source_ = std::move(other.source_);
      segments_ = std::move(other.segments_);
      size_ = other.size_;
      capacity_ = other.capacity_;
      other.size_ = 0;
      other.capacity_ = 0;
      other.segments_.clear();
    }
    return *this;
  }

  constexpr ~SegmentedSequence() { release(); }

  constexpr void swap(SegmentedSequence& other) noexcept
  requires std::is_nothrow_swappable_v<Source>
  {
    using std::swap;
    swap(source_, other.source_);
    swap(segments_, other.segments_);
    swap(size_, other.size_);
    swap(capacity_, other.capacity_);
  }

  friend constexpr void swap(SegmentedSequence& lhs, SegmentedSequence& rhs) noexcept
  requires std::is_nothrow_swappable_v<Source>
  {
    lhs.swap(rhs);
  }

  constexpr bool empty() const noexcept { return size_ == 0; }

  constexpr size_type size() const noexcept { return size_; }

  constexpr size_type capacity() const noexcept { return capacity_; }

  static constexpr size_type max_size() noexcept { return Options.maximum_size; }

  constexpr size_type segment_count() const noexcept { return segments_.size(); }

  constexpr size_type bytes_reserved() const noexcept {
    size_type result = 0;
    for (const Segment& segment : segments_) {
      result += segment.block.size;
    }
    return result;
  }

  constexpr reference operator[](size_type pos) noexcept { return ElementAt(pos); }

  constexpr const_reference operator[](size_type pos) const noexcept { return ElementAt(pos); }

  constexpr reference at(size_type pos) {
    MBO_CONFIG_REQUIRE(pos < size_, "SegmentedSequence index is out of range");
    return (*this)[pos];
  }

  constexpr const_reference at(size_type pos) const {
    MBO_CONFIG_REQUIRE(pos < size_, "SegmentedSequence index is out of range");
    return (*this)[pos];
  }

  constexpr reference front() { return at(0); }

  constexpr const_reference front() const { return at(0); }

  constexpr reference back() { return at(size_ - 1); }

  constexpr const_reference back() const { return at(size_ - 1); }

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
  constexpr std::optional<std::reference_wrapper<T>> try_emplace_back(Args&&... args)
  requires Source::supports_recoverable_failure
  {
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
  constexpr reference unchecked_emplace_back(Args&&... args) {
    MBO_CONFIG_REQUIRE(size_ < capacity_, "SegmentedSequence unchecked append requires reserved capacity");
    const auto [segment_index, offset] = Locate(size_);
    Segment& segment = segments_[segment_index];
    MBO_CONFIG_REQUIRE(offset == segment.size, "SegmentedSequence segment prefix is inconsistent");
    T* const result = std::construct_at(segment.data + offset, std::forward<Args>(args)...);
    ++segment.size;
    ++size_;
    return *result;
  }

  constexpr reference push_back(const T& value) { return emplace_back(value); }

  constexpr reference push_back(T&& value) { return emplace_back(std::move(value)); }

  constexpr std::optional<std::reference_wrapper<T>> try_push_back(const T& value)
  requires Source::supports_recoverable_failure
  {
    return try_emplace_back(value);
  }

  constexpr std::optional<std::reference_wrapper<T>> try_push_back(T&& value)
  requires Source::supports_recoverable_failure
  {
    return try_emplace_back(std::move(value));
  }

  constexpr reference unchecked_push_back(const T& value) { return unchecked_emplace_back(value); }

  constexpr reference unchecked_push_back(T&& value) { return unchecked_emplace_back(std::move(value)); }

  constexpr void reserve(size_type requested) {
    MBO_CONFIG_REQUIRE(requested <= max_size(), "SegmentedSequence reserve exceeds max_size");
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
  requires std::default_initializable<T>
  {
    if (requested < size_) {
      while (size_ != requested) {
        pop_back();
      }
      return;
    }
    reserve(requested);
    while (size_ != requested) {
      unchecked_emplace_back();
    }
  }

  constexpr void resize(size_type requested, const T& value)
  requires std::copy_constructible<T>
  {
    if (requested < size_) {
      while (size_ != requested) {
        pop_back();
      }
      return;
    }
    reserve(requested);
    while (size_ != requested) {
      unchecked_emplace_back(value);
    }
  }

  constexpr void pop_back() noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(!empty(), "Cannot pop from an empty SegmentedSequence");
    const auto [segment_index, offset] = Locate(size_ - 1);
    Segment& segment = segments_[segment_index];
    MBO_CONFIG_REQUIRE(offset + 1 == segment.size, "SegmentedSequence segment prefix is inconsistent");
    --segment.size;
    --size_;
    std::destroy_at(segment.data + offset);
  }

  constexpr T pop_back_value() noexcept
  requires std::is_nothrow_move_constructible_v<T>
  {
    T result(std::move(back()));
    pop_back();
    return result;
  }

  constexpr void clear() noexcept {
    while (!empty()) {
      pop_back();
    }
  }

  constexpr void trim_capacity() noexcept {
    while (!segments_.empty() && segments_.back().size == 0) {
      ReleaseLastEmptySegment();
    }
  }

  constexpr void trim_capacity(size_type requested) noexcept {
    const std::size_t target = requested < size_ ? size_ : requested;
    while (!segments_.empty() && segments_.back().size == 0 && capacity_ - segments_.back().capacity >= target) {
      ReleaseLastEmptySegment();
    }
  }

  constexpr void release() noexcept {
    clear();
    for (auto pos = segments_.rbegin(); pos != segments_.rend(); ++pos) {
      source_.Release(pos->block);
    }
    segments_.clear();
    capacity_ = 0;
  }

  constexpr segment_range segments() noexcept { return segment_range(this, LiveSegmentCount()); }

  constexpr const_segment_range segments() const noexcept { return const_segment_range(this, LiveSegmentCount()); }

 private:
  static constexpr std::size_t CapacityForSegment(std::size_t segment_index) noexcept {
    if (segment_index < Options.listed_capacities) {
      return Options.segment_capacities[segment_index];
    }
    return Options.repeat_last ? Options.segment_capacities[Options.listed_capacities - 1] : 0;
  }

  static constexpr std::pair<std::size_t, std::size_t> Locate(std::size_t pos) noexcept {
    if constexpr (Options.listed_capacities == 1 && Options.repeat_last) {
      const std::size_t capacity = Options.segment_capacities.front();
      return {pos / capacity, pos % capacity};
    }
    if constexpr (Options.repeat_last) {
      constexpr std::size_t kListedCapacity = [] {
        std::size_t result = 0;
        for (std::size_t segment_index = 0; segment_index < Options.listed_capacities; ++segment_index) {
          result += Options.segment_capacities[segment_index];
        }
        return result;
      }();
      if (pos >= kListedCapacity) {
        const std::size_t repeated = Options.segment_capacities[Options.listed_capacities - 1];
        const std::size_t tail = pos - kListedCapacity;
        return {Options.listed_capacities + (tail / repeated), tail % repeated};
      }
    }
    for (std::size_t segment_index = 0; segment_index < Options.listed_capacities; ++segment_index) {
      const std::size_t capacity = Options.segment_capacities[segment_index];
      if (pos < capacity) {
        return {segment_index, pos};
      }
      pos -= capacity;
    }
    return {Options.listed_capacities, pos};
  }

  constexpr std::size_t LiveSegmentCount() const noexcept { return empty() ? 0 : Locate(size_ - 1).first + 1; }

  constexpr reference ElementAt(std::size_t pos) noexcept {
    const auto [segment_index, offset] = Locate(pos);
    return segments_[segment_index].data[offset];
  }

  constexpr const_reference ElementAt(std::size_t pos) const noexcept {
    const auto [segment_index, offset] = Locate(pos);
    return segments_[segment_index].data[offset];
  }

  constexpr bool TryAddSegment() {
    const std::size_t segment_capacity = CapacityForSegment(segments_.size());
    if (segment_capacity == 0 || capacity_ >= max_size() || segment_capacity > max_size() - capacity_
        || segment_capacity > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      return false;
    }
    const auto block = source_.TryAcquire(segment_capacity * sizeof(T), alignof(T));
    if (!block || block->data == nullptr || block->size < segment_capacity * sizeof(T) || block->alignment < alignof(T)
        || std::bit_cast<std::uintptr_t>(block->data) % alignof(T) != 0) {
      if (block) {
        source_.Release(*block);
      }
      return false;
    }
#if __cpp_exceptions
    try {
#endif
      segments_.push_back(Segment{
          .block = *block,
          .data = reinterpret_cast<T*>(block->data),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
          .capacity = segment_capacity,
          .size = 0,
      });
#if __cpp_exceptions
    } catch (...) {
      source_.Release(*block);
      return false;
    }
#endif
    capacity_ += segment_capacity;
    return true;
  }

  constexpr void ReleaseLastEmptySegment() noexcept {
    const Segment& segment = segments_.back();
    source_.Release(segment.block);
    capacity_ -= segment.capacity;
    segments_.pop_back();
  }

  constexpr void ReleaseSegmentsFrom(std::size_t first) noexcept {
    while (segments_.size() > first) {
      ReleaseLastEmptySegment();
    }
  }

  [[no_unique_address]] Source source_{};
  std::vector<Segment> segments_;
  std::size_t size_ = 0;
  std::size_t capacity_ = 0;
};

}  // namespace mbo::container

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// NOLINTEND(readability-identifier-naming)

#endif  // MBO_CONTAINER_SEGMENTED_SEQUENCE_H_
