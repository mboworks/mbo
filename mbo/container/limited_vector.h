// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MBO_CONTAINER_LIMITED_VECTOR_H_
#define MBO_CONTAINER_LIMITED_VECTOR_H_

#include <algorithm>
#include <compare>   // IWYU pragma: keep
#include <concepts>  // IWYU pragma: keep
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>  // IWYU pragma: keep
#include <type_traits>
#include <utility>

#include "mbo/config/config.h"
#include "mbo/config/require.h"
#include "mbo/container/limited_options.h"  // IWYU pragma: export
#include "mbo/types/traits.h"

namespace mbo::container {

// NOLINTBEGIN(*-pro-type-union-access,*-pro-bounds-constant-array-index,*-pro-bounds-pointer-arithmetic,*-no-array-decay,*-array-to-pointer-decay)
// LimitedVector keeps its elements in a union so that unused capacity stays
// uninitialized. Indexing that storage and reading the active member is the
// container's implementation, not unchecked use of a container.

// NOLINTBEGIN(readability-identifier-naming)

template<typename T>
concept LimitedVectorValid = std::move_constructible<std::remove_const_t<T>>;

// Implements a `std::vector` like container that only uses inlined memory. So if used as a local
// variable with a types that does not perform memory allocation, then this type does not perform
// any memory allocation.
//
// Unlike `std::array` this type can vary in size.
//
// Iterators are random-access but not contiguous: elements are active
// subobjects of separate union slots, not elements of a `T[]` array.
// Consequently, pointer arithmetic from the first element is not valid, so
// `LimitedVector` does not expose `data()`.
//
// The type is fully `constexpr` compliant and can be used as a `static constexpr`.
//
// Can be constructed with helpers `MakeLimitedVector` or `ToLimitedVector`.
//
// Example:
//
// ```c++
// using mbo::container::LimitedVector;
//
// constexpr auto kMyData = MakeLimitedVector(1, 2, 3, 4);
// ```
//
// The above example infers the value_type to be `int` as it is the common type of the arguments.
// The resulting `LimitedVector` has a capacity of 4 and the elements {1, 2, 3, 4}.
template<typename T, auto CapacityOrOptions>
requires(LimitedVectorValid<T>)
class LimitedVector final {
 private:
  struct None final {};

  using RawValue = std::remove_const_t<T>;

  static_assert(IsLimitedOptionsOrSize<decltype(CapacityOrOptions)>);
  using Options = decltype(MakeLimitedOptions<CapacityOrOptions>());
  // static_assert(std::is_trivially_destructible_v<T> || Options::Has(LimitedOptionsFlag::kEmptyDestructor));
  static constexpr std::size_t Capacity = Options::kCapacity;

  static constexpr bool kRequireThrows = ::mbo::config::kRequireThrows;

  union Data {
    constexpr Data() noexcept : none{} {}

    constexpr Data(const Data&) noexcept = default;
    constexpr Data& operator=(const Data&) noexcept = default;
    constexpr Data(Data&&) noexcept = default;
    constexpr Data& operator=(Data&&) noexcept = default;

    constexpr ~Data() noexcept
    requires(std::is_trivially_destructible_v<RawValue>)
    = default;

    constexpr ~Data() noexcept
    requires(!std::is_trivially_destructible_v<RawValue>)
    {}

    None none;
    // Store the non-const value type so that constexpr placement-`construct_at`
    // does not modify a const-qualified object (clang 21 rejects that, even
    // through the `const_cast` below). Constness is reintroduced by the public
    // accessors via `reference`/`const_reference`/`pointer`/`const_pointer`.
    RawValue data;
  };

  // A throwing constructor never runs `LimitedVector`'s destructor. Track the
  // partially constructed container so its already-live elements are destroyed
  // unless the constructor completes and releases the guard.
  class ConstructionGuard final {
   public:
    constexpr explicit ConstructionGuard(LimitedVector* target) noexcept : target_(target) {}

    constexpr ~ConstructionGuard() noexcept {
      if (target_ != nullptr) {
        target_->clear();
      }
    }

    ConstructionGuard(const ConstructionGuard&) = delete;
    ConstructionGuard& operator=(const ConstructionGuard&) = delete;
    ConstructionGuard(ConstructionGuard&&) = delete;
    ConstructionGuard& operator=(ConstructionGuard&&) = delete;

    constexpr void Release() noexcept { target_ = nullptr; }

   private:
    LimitedVector* target_;
  };

  // Must declare each other as friends so that we can correctly move from other.
  template<typename U, auto OtherCapacityOrOptions>
  requires(LimitedVectorValid<U>)
  friend class LimitedVector;

 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;

  template<bool IsConst>
  class Iterator final {
   private:
    template<bool>
    friend class Iterator;

    using DataPointer = std::conditional_t<IsConst, const Data*, Data*>;

   public:
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::random_access_iterator_tag;
    using value_type = RawValue;
    using difference_type = LimitedVector::difference_type;
    using pointer = std::conditional_t<IsConst, LimitedVector::const_pointer, LimitedVector::pointer>;
    using reference = std::conditional_t<IsConst, LimitedVector::const_reference, LimitedVector::reference>;

    constexpr Iterator() noexcept = default;

    constexpr explicit Iterator(DataPointer base, difference_type pos) noexcept : base_(base), pos_(pos) {}

    template<bool OtherConst>
    requires(IsConst && !OtherConst)
    // Deliberately implicit: a mutable iterator must convert to const_iterator.
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr Iterator(const Iterator<OtherConst>& other) noexcept : base_(other.base_), pos_(other.pos_) {}

    constexpr reference operator*() const noexcept { return base_[pos_].data; }

    constexpr pointer operator->() const noexcept { return &base_[pos_].data; }

    constexpr Iterator& operator++() noexcept {
      ++pos_;
      return *this;
    }

    constexpr Iterator operator++(int) noexcept {
      const Iterator result = *this;
      ++*this;
      return result;
    }

    constexpr Iterator& operator--() noexcept {
      --pos_;
      return *this;
    }

    constexpr Iterator operator--(int) noexcept {
      const Iterator result = *this;
      --*this;
      return result;
    }

    constexpr Iterator& operator+=(difference_type offset) noexcept {
      pos_ += offset;
      return *this;
    }

    constexpr Iterator& operator-=(difference_type offset) noexcept {
      pos_ -= offset;
      return *this;
    }

    constexpr reference operator[](difference_type offset) const noexcept { return base_[pos_ + offset].data; }

    friend constexpr Iterator operator+(Iterator iter, difference_type offset) noexcept { return iter += offset; }

    friend constexpr Iterator operator+(difference_type offset, Iterator iter) noexcept { return iter += offset; }

    friend constexpr Iterator operator-(Iterator iter, difference_type offset) noexcept { return iter -= offset; }

    template<bool OtherConst>
    constexpr difference_type operator-(const Iterator<OtherConst>& other) const noexcept {
      return pos_ - other.pos_;
    }

    template<bool OtherConst>
    constexpr bool operator==(const Iterator<OtherConst>& other) const noexcept {
      return base_ == other.base_ && pos_ == other.pos_;
    }

    template<bool OtherConst>
    constexpr auto operator<=>(const Iterator<OtherConst>& other) const noexcept {
      return pos_ <=> other.pos_;
    }

   private:
    DataPointer base_{nullptr};
    difference_type pos_{0};
  };

  using iterator = Iterator<false>;
  using const_iterator = Iterator<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  // Destructor and constructors from same type.
  constexpr ~LimitedVector() noexcept
  requires(Options::Has(LimitedOptionsFlag::kEmptyDestructor))
  = default;

  constexpr ~LimitedVector() noexcept
  requires(!Options::Has(LimitedOptionsFlag::kEmptyDestructor) && std::is_trivially_destructible_v<RawValue>)
  {
    clear();
  }

  constexpr ~LimitedVector() noexcept
  requires(!Options::Has(LimitedOptionsFlag::kEmptyDestructor) && !std::is_trivially_destructible_v<RawValue>)
  {
    clear();
  }

  constexpr LimitedVector() noexcept = default;

  constexpr LimitedVector(const LimitedVector& other) noexcept(std::is_nothrow_copy_constructible_v<RawValue>) {
    ConstructionGuard guard(this);
    for (const_reference value : other) {
      emplace_back(value);
    }
    guard.Release();
  }

  constexpr LimitedVector& operator=(const LimitedVector& other) noexcept(
      std::is_nothrow_copy_constructible_v<RawValue>) {
    if (this != &other) {
      clear();
      for (const_reference value : other) {
        emplace_back(value);
      }
    }
    return *this;
  }

  constexpr LimitedVector(LimitedVector&& other) noexcept(std::is_nothrow_move_constructible_v<RawValue>) {
    ConstructionGuard guard(this);
    // Must stay mutable for move-only types, even when scalar instantiations
    // make clang-tidy believe const would be sufficient.
    for (reference value : other) {  // NOLINT(misc-const-correctness)
      emplace_back(std::move(value));
    }
    guard.Release();
    other.clear();
  }

  constexpr LimitedVector& operator=(LimitedVector&& other) noexcept(std::is_nothrow_move_constructible_v<RawValue>) {
    if (this != &other) {
      clear();
      // Must stay mutable for move-only types, even when scalar instantiations
      // make clang-tidy believe const would be sufficient.
      for (reference value : other) {  // NOLINT(misc-const-correctness)
        emplace_back(std::move(value));
      }
      other.clear();
    }
    return *this;
  }

  // Constructors and assignment from other LimitVector/value types.

  template<std::forward_iterator It>
  requires types::ConstructibleFrom<T, mbo::types::ForwardIteratorValueType<It>>
  constexpr LimitedVector(It begin, It end) noexcept(
      !kRequireThrows && std::is_nothrow_constructible_v<RawValue, std::iter_reference_t<It>>) {
    ConstructionGuard guard(this);
    while (begin != end) {
      emplace_back(*begin++);
    }
    guard.Release();
  }

  constexpr LimitedVector(const std::initializer_list<T>& list) noexcept(
      !kRequireThrows && std::is_nothrow_copy_constructible_v<RawValue>)
      : LimitedVector(list.begin(), list.end()) {}

  template<types::ConstructibleInto<T> U>
  constexpr LimitedVector(const std::initializer_list<U>& list) noexcept(
      !kRequireThrows && std::is_nothrow_constructible_v<RawValue, const U&>)
      : LimitedVector(list.begin(), list.end()) {}

  template<types::ConstructibleInto<T> U>
  constexpr LimitedVector& operator=(const std::initializer_list<U>& list) {
    assign(list.begin(), list.end());
    return *this;
  }

  template<types::ConstructibleInto<T> U, auto OtherN>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  constexpr explicit LimitedVector(const LimitedVector<U, OtherN>& other) noexcept(
      !kRequireThrows && std::is_nothrow_constructible_v<RawValue, const U&>) {
    ConstructionGuard guard(this);
    for (const auto& value : other) {
      emplace_back(value);
    }
    guard.Release();
  }

  template<types::ConstructibleInto<T> U, auto OtherN>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  constexpr LimitedVector& operator=(const LimitedVector<U, OtherN>& other) noexcept(
      !kRequireThrows && std::is_nothrow_constructible_v<RawValue, const U&>) {
    clear();
    for (const auto& value : other) {
      emplace_back(value);
    }
    return *this;
  }

  template<types::ConstructibleInto<T> U, auto OtherN>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  // Moved element-wise below; `other` has a different type, so it cannot be moved as a whole.
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  constexpr explicit LimitedVector(LimitedVector<U, OtherN>&& other) noexcept(
      !kRequireThrows && std::is_nothrow_constructible_v<RawValue, U&&>) {
    ConstructionGuard guard(this);
    for (auto& value : other) {
      emplace_back(std::move(value));
    }
    guard.Release();
    other.clear();
  }

  template<types::ConstructibleInto<T> U, auto OtherN>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  // Moved element-wise below; `other` has a different type, so it cannot be moved as a whole.
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  constexpr LimitedVector& operator=(LimitedVector<U, OtherN>&& other) noexcept(
      !kRequireThrows && std::is_nothrow_constructible_v<RawValue, U&&>) {
    clear();
    for (auto& value : other) {
      emplace_back(std::move(value));
    }
    other.clear();
    return *this;
  }

  // Modification: clear, resize, reserve, explace_back, push_back, pop_back, assign, insert

  constexpr void clear() noexcept {
    while (!empty()) {
      pop_back();
    }
  }

  constexpr void resize(std::size_t new_size) noexcept(
      !kRequireThrows && std::is_nothrow_default_constructible_v<RawValue>) {
    MBO_CONFIG_REQUIRE(new_size <= Capacity, "Cannot resize beyond capacity.");
    while (new_size < size()) {
      pop_back();
    }
    while (new_size > size()) {
      emplace_back();
    }
  }

  constexpr void reserve(std::size_t size) noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(size <= Capacity, "Cannot reserve beyond capacity.");
  }

  constexpr void shrink_to_fit() noexcept {
    // Nothing to do. The contract says there is no requirement to reduce capacity.
  }

  template<typename U, auto OtherN>
  requires(std::same_as<T, U> && MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  constexpr void swap(LimitedVector<U, OtherN>& other) noexcept(
      !kRequireThrows && std::is_nothrow_swappable_v<RawValue> && std::is_nothrow_move_constructible_v<RawValue>) {
    if (static_cast<const void*>(this) == static_cast<const void*>(&other)) {
      return;
    }
    MBO_CONFIG_REQUIRE(size_ <= other.capacity(), "Cannot swap beyond capacity.");
    const size_type common_size = std::min(size_, other.size_);
    for (size_type pos = 0; pos < common_size; ++pos) {
      std::swap(values_[pos].data, other.values_[pos].data);
    }
    if (size_ > common_size) {
      const size_type old_size = size_;
      for (size_type pos = common_size; pos < old_size; ++pos) {
        other.emplace_back(std::move(values_[pos].data));
      }
      while (size_ > common_size) {
        pop_back();
      }
    } else {
      const size_type old_other_size = other.size_;
      for (size_type pos = common_size; pos < old_other_size; ++pos) {
        emplace_back(std::move(other.values_[pos].data));
      }
      while (other.size_ > common_size) {
        other.pop_back();
      }
    }
  }

  template<typename... Args>
  requires std::assignable_from<RawValue&, RawValue&&>
  constexpr iterator emplace(const_iterator pos, Args&&... args) noexcept(
      !kRequireThrows && std::is_nothrow_constructible_v<RawValue, Args...>
      && std::is_nothrow_move_constructible_v<RawValue> && std::is_nothrow_move_assignable_v<RawValue>) {
    MBO_CONFIG_REQUIRE(size_ < Capacity, "Called `emplace` at capacity.");
    MBO_CONFIG_REQUIRE(cbegin() <= pos && pos <= cend(), "Invalid `pos`.");
    const auto index = static_cast<size_type>(pos - cbegin());
    // Staged before mutation for alias safety, then moved into the container.
    RawValue value(std::forward<Args>(args)...);  // NOLINT(misc-const-correctness)
    if (index == size_) {
      emplace_back(std::move(value));
      return begin() + static_cast<difference_type>(index);
    }
    const size_type old_size = size_;
    emplace_back(std::move(values_[old_size - 1].data));
    for (size_type dst = old_size - 1; dst > index; --dst) {
      values_[dst].data = std::move(values_[dst - 1].data);
    }
    values_[index].data = std::move(value);
    return begin() + static_cast<difference_type>(index);
  }

  constexpr iterator erase(const_iterator pos) noexcept(
      !kRequireThrows && std::is_nothrow_move_assignable_v<RawValue>) {
    MBO_CONFIG_REQUIRE(cbegin() <= pos && pos < cend(), "Invalid `pos`.");
    const auto index = static_cast<size_type>(pos - cbegin());
    for (size_type dst = index; dst + 1 < size_; ++dst) {
      values_[dst].data = std::move(values_[dst + 1].data);
    }
    pop_back();
    return begin() + static_cast<difference_type>(index);
  }

  constexpr iterator erase(const_iterator first, const_iterator last) noexcept(
      !kRequireThrows && std::is_nothrow_move_assignable_v<RawValue>) {
    MBO_CONFIG_REQUIRE(cbegin() <= first && first <= last && last <= cend(), "Invalid `first` or `last`.");
    const auto first_index = static_cast<size_type>(first - cbegin());
    const auto deleted = static_cast<size_type>(last - first);
    for (size_type dst = first_index; dst + deleted < size_; ++dst) {
      values_[dst].data = std::move(values_[dst + deleted].data);
    }
    for (size_type count = 0; count < deleted; ++count) {
      pop_back();
    }
    return begin() + static_cast<difference_type>(first_index);
  }

  template<typename... Args>
  constexpr reference emplace_back(Args&&... args) noexcept(
      !kRequireThrows && std::is_nothrow_constructible_v<RawValue, Args...>) {
    MBO_CONFIG_REQUIRE(size_ < Capacity, "Called `emplace_back` at capacity.");
    auto& data_ref = values_[size_];
    std::construct_at(&data_ref.data, std::forward<Args>(args)...);
    ++size_;
    return data_ref.data;
  }

  constexpr reference push_back(T&& val) noexcept(!kRequireThrows && std::is_nothrow_constructible_v<RawValue, T&&>) {
    return emplace_back(std::move(val));
  }

  constexpr reference push_back(const T& val) noexcept(
      !kRequireThrows && std::is_nothrow_constructible_v<RawValue, const T&>) {
    return emplace_back(val);
  }

  constexpr void pop_back() noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(size_ > 0, "No element to pop.");
    std::destroy_at(&values_[--size_].data);
  }

  constexpr void assign(std::size_t num, const T& value) {
    MBO_CONFIG_REQUIRE(num <= Capacity, "Called `assign` beyond capacity.");
    const RawValue value_copy(value);
    clear();
    for (; num > 0; --num) {
      push_back(value_copy);
    }
  }

  template<std::forward_iterator It>
  constexpr void assign(It begin, It end) {
    // Staged before clear for alias safety, then moved into this container.
    LimitedVector<RawValue, Capacity> values(begin, end);  // NOLINT(misc-const-correctness)
    clear();
    for (auto& value : values) {
      emplace_back(std::move(value));
    }
  }

  constexpr void assign(const std::initializer_list<T>& list) {
    MBO_CONFIG_REQUIRE(list.size() <= Capacity, "Called `assign` at capacity.");
    assign(list.begin(), list.end());
  }

  template<types::ConstructibleInto<T> U>
  constexpr iterator insert(const_iterator pos, U&& value) {
    return emplace(pos, std::forward<U>(value));
  }

  constexpr iterator insert(const_iterator pos, size_type count, const T& value) {
    MBO_CONFIG_REQUIRE(size_ + count <= Capacity, "Called `insert` at capacity.");
    MBO_CONFIG_REQUIRE(cbegin() <= pos && pos <= cend(), "Invalid `pos`.");
    const auto index = static_cast<size_type>(pos - cbegin());
    if (count == 0) {
      return begin() + static_cast<difference_type>(index);
    }
    const RawValue value_copy(value);
    iterator dst = begin() + static_cast<difference_type>(index);
    for (size_type inserted = 0; inserted < count; ++inserted) {
      dst = emplace(dst, value_copy);
      ++dst;
    }
    return begin() + static_cast<difference_type>(index);
  }

  constexpr iterator insert(const_iterator pos, const T& value) { return insert(pos, 1, value); }

  template<std::input_iterator InputIt>
  requires(types::ConstructibleFrom<T, std::iter_reference_t<InputIt>>)
  constexpr iterator insert(const_iterator pos, InputIt first, InputIt last) {
    MBO_CONFIG_REQUIRE(cbegin() <= pos && pos <= cend(), "Invalid `pos`.");
    const auto index = static_cast<size_type>(pos - cbegin());
    LimitedVector<RawValue, Capacity> incoming;
    while (first != last) {
      incoming.emplace_back(*first++);
    }
    MBO_CONFIG_REQUIRE(size_ + incoming.size() <= Capacity, "Called `insert` at capacity.");
    if (incoming.empty()) {
      return begin() + static_cast<difference_type>(index);
    }
    iterator dst = begin() + static_cast<difference_type>(index);
    for (auto& value : incoming) {
      dst = emplace(dst, std::move(value));
      ++dst;
    }
    return begin() + static_cast<difference_type>(index);
  }

  template<types::ConstructibleInto<T> U>
  constexpr iterator insert(const_iterator pos, std::initializer_list<U> list) {
    return insert(pos, list.begin(), list.end());
  }

  // Read/write access

  constexpr std::size_t size() const noexcept { return size_; }

  constexpr std::size_t max_size() const noexcept { return Capacity; }

  constexpr std::size_t capacity() const noexcept { return Capacity; }

  constexpr bool empty() const noexcept { return size_ == 0; }

  constexpr reference front() noexcept { return values_[0].data; }

  constexpr const_reference front() const noexcept { return values_[0].data; }

  constexpr reference back() noexcept { return values_[size_ > 0 ? size_ - 1 : 0].data; }

  constexpr const_reference back() const noexcept { return values_[size_ > 0 ? size_ - 1 : 0].data; }

  constexpr iterator begin() noexcept { return iterator(values_, 0); }

  constexpr const_iterator begin() const noexcept { return const_iterator(values_, 0); }

  constexpr const_iterator cbegin() const noexcept { return const_iterator(values_, 0); }

  constexpr iterator end() noexcept { return iterator(values_, static_cast<difference_type>(size_)); }

  constexpr const_iterator end() const noexcept { return const_iterator(values_, static_cast<difference_type>(size_)); }

  constexpr const_iterator cend() const noexcept {
    return const_iterator(values_, static_cast<difference_type>(size_));
  }

  constexpr reverse_iterator rbegin() noexcept { return std::make_reverse_iterator(end()); }

  constexpr const_reverse_iterator rbegin() const noexcept { return std::make_reverse_iterator(end()); }

  constexpr const_reverse_iterator crbegin() const noexcept { return std::make_reverse_iterator(end()); }

  constexpr reverse_iterator rend() noexcept { return std::make_reverse_iterator(begin()); }

  constexpr const_reverse_iterator rend() const noexcept { return std::make_reverse_iterator(begin()); }

  constexpr const_reverse_iterator crend() const noexcept { return std::make_reverse_iterator(cbegin()); }

  constexpr reference operator[](std::size_t index) noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(index < size_, "Access past size.");
    return values_[index].data;
  }

  constexpr reference at(std::size_t index) noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(index < size_, "Access past size.");
    return values_[index].data;
  }

  constexpr const_reference operator[](std::size_t index) const noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(index < size_, "Access past size.");
    return values_[index].data;
  }

  constexpr const_reference at(std::size_t index) const noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(index < size_, "Access past size.");
    return values_[index].data;
  }

 private:
  std::size_t size_{0};
  // Array would be better but that does not work with ASAN builds.
  // std::array<Data, Capacity == 0 ? 1 : Capacity> values_;
  // Add an unused sentinel, so that `end` and other functions do not cause memory issues.
  Data values_[Capacity + 1];  // NOLINT(*-avoid-c-arrays)
};

template<typename... T>
LimitedVector(T&&... v) -> LimitedVector<std::common_type_t<T...>, sizeof...(T)>;

// NOLINTBEGIN(*-avoid-unchecked-container-access): LimitedVector's own
// comparison implementation - every index is below min(lhs.size(), rhs.size()),
// and going through the checked operator[] would re-verify that per element.
template<auto LN, auto RN, typename LHS, typename RHS>
requires std::three_way_comparable_with<LHS, RHS>
constexpr inline auto operator<=>(const LimitedVector<LHS, LN>& lhs, const LimitedVector<RHS, RN>& rhs) {
  const std::size_t minsize = std::min(lhs.size(), rhs.size());
  for (std::size_t index = 0; index < minsize; ++index) {
    const auto comp = lhs[index] <=> rhs[index];
    if (comp != 0) {
      return comp;
    }
  }
  return lhs.size() <=> rhs.size();
}

template<auto LN, auto RN, typename LHS, typename RHS>
requires std::three_way_comparable_with<LHS, RHS>
constexpr inline bool operator==(const LimitedVector<LHS, LN>& lhs, const LimitedVector<RHS, RN>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  const std::size_t minsize = std::min(lhs.size(), rhs.size());
  for (std::size_t index = 0; index < minsize; ++index) {
    const auto comp = lhs[index] <=> rhs[index];
    if (comp != 0) {
      return false;
    }
  }
  return true;
}

template<auto LN, auto RN, typename LHS, typename RHS>
requires std::three_way_comparable_with<LHS, RHS>
constexpr inline bool operator<(const LimitedVector<LHS, LN>& lhs, const LimitedVector<RHS, RN>& rhs) {
  const std::size_t minsize = std::min(lhs.size(), rhs.size());
  for (std::size_t index = 0; index < minsize; ++index) {
    const auto comp = lhs[index] <=> rhs[index];
    if (comp != 0) {
      return comp < 0;
    }
  }
  return lhs.size() < rhs.size();
}

// NOLINTEND(*-avoid-unchecked-container-access)

template<typename T, std::size_t N = 0, LimitedOptionsFlag... Flags>
inline constexpr auto MakeLimitedVector() noexcept {
  return LimitedVector<T, LimitedOptions<N, Flags...>{}>();
}

template<std::size_t N, LimitedOptionsFlag... Flags, std::forward_iterator It>
inline constexpr auto MakeLimitedVector(It&& begin, It&& end) noexcept(
    !::mbo::config::kRequireThrows
    && std::is_nothrow_constructible_v<mbo::types::ForwardIteratorValueType<It>, std::iter_reference_t<It>>) {
  return LimitedVector<mbo::types::ForwardIteratorValueType<It>, LimitedOptions<N, Flags...>{}>(
      std::forward<It>(begin), std::forward<It>(end));
}

template<std::size_t N, typename T, LimitedOptionsFlag... Flags>
inline constexpr auto MakeLimitedVector(const std::initializer_list<T>& data) {
  MBO_CONFIG_REQUIRE(data.size() <= N, "Too many initlizer values.");
  return LimitedVector<T, LimitedOptions<N, Flags...>{}>(data);
}

template<std::size_t N, typename T, LimitedOptionsFlag... Flags>
requires(N > 0 && mbo::types::NotInitializerList<T>)
inline constexpr auto MakeLimitedVector(const T& value) noexcept(
    !::mbo::config::kRequireThrows && std::is_nothrow_copy_constructible_v<T>) {
  auto result = LimitedVector<T, LimitedOptions<N, Flags...>{}>();
  result.assign(N, value);
  return result;
}

template<typename... Args>
requires((types::NotInitializerList<Args> && !std::forward_iterator<Args> && !types::IsCharArray<Args>) && ...)
inline constexpr auto MakeLimitedVector(Args&&... args) noexcept(
    !::mbo::config::kRequireThrows && (std::is_nothrow_constructible_v<std::common_type_t<Args...>, Args&&> && ...)) {
  using T = std::common_type_t<Args...>;
  auto result = LimitedVector<T, sizeof...(Args)>();
  (result.emplace_back(std::forward<Args>(args)), ...);
  return result;
}

// This specialization takes `const char*` and `const char(&)[N]` arguments and creates an appropriately sized container
// of type `LimitedVector<std::string_view>`.
template<int&..., types::IsCharArray... Args>
inline constexpr auto MakeLimitedVector(Args... args) noexcept {
  auto result = LimitedVector<std::string_view, sizeof...(Args)>();
  (result.emplace_back(std::string_view(args)), ...);
  return result;
}

template<types::NotIsCharArray T, int&..., types::IsCharArray... Args>
inline constexpr auto MakeLimitedVector(Args... args) noexcept(
    !::mbo::config::kRequireThrows && (std::is_nothrow_constructible_v<T, Args> && ...)) {
  auto result = LimitedVector<T, sizeof...(Args)>();
  (result.emplace_back(T(args)), ...);
  return result;
}

// NOLINTBEGIN(*-avoid-c-arrays)
template<typename T, LimitedOptionsFlag... Flags, int&..., std::size_t N>
constexpr LimitedVector<std::remove_cvref_t<T>, LimitedOptions<N, Flags...>{}> ToLimitedVector(T (&array)[N]) {
  LimitedVector<std::remove_cvref_t<T>, LimitedOptions<N, Flags...>{}> result;
  for (std::size_t idx = 0; idx < N; ++idx) {
    result.emplace_back(array[idx]);
  }
  return result;
}

template<typename T, LimitedOptionsFlag... Flags, int&..., std::size_t N>
// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved): an array is moved element-wise.
constexpr LimitedVector<std::remove_cvref_t<T>, LimitedOptions<N, Flags...>{}> ToLimitedVector(T (&&array)[N]) {
  LimitedVector<std::remove_cvref_t<T>, LimitedOptions<N, Flags...>{}> result;
  for (std::size_t idx = 0; idx < N; ++idx) {
    result.emplace_back(std::move(array[idx]));
  }
  return result;
}

// NOLINTEND(*-avoid-c-arrays)

// NOLINTEND(readability-identifier-naming)

// NOLINTEND(*-pro-type-union-access,*-pro-bounds-constant-array-index,*-pro-bounds-pointer-arithmetic,*-no-array-decay,*-array-to-pointer-decay)

}  // namespace mbo::container

#endif  // MBO_CONTAINER_LIMITED_VECTOR_H_
