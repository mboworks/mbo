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
#include <functional>
#include <initializer_list>
#include <memory>
#include <new>  // IWYU pragma: keep
#include <optional>
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
    requires(std::is_trivially_destructible_v<T>)
    = default;

    constexpr ~Data() noexcept
    requires(!std::is_trivially_destructible_v<T>)
    {}

    None none;
    // Store the non-const value type so that constexpr placement-`construct_at`
    // does not modify a const-qualified object (clang 21 rejects that, even
    // through the `const_cast` below). Constness is reintroduced by the public
    // accessors via `reference`/`const_reference`/`pointer`/`const_pointer`.
    std::remove_const_t<T> data;
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
  using iterator = T*;
  using const_iterator = const T*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  // Destructor and constructors from same type.
  constexpr ~LimitedVector() noexcept
  requires(Options::Has(LimitedOptionsFlag::kEmptyDestructor))
  = default;

  constexpr ~LimitedVector() noexcept
  requires(!Options::Has(LimitedOptionsFlag::kEmptyDestructor) && std::is_trivially_destructible_v<T>)
  {
    clear();
  }

  ~LimitedVector() noexcept
  requires(!Options::Has(LimitedOptionsFlag::kEmptyDestructor) && !std::is_trivially_destructible_v<T>)
  {
    clear();
  }

  constexpr LimitedVector() noexcept = default;

  constexpr LimitedVector(const LimitedVector& other) noexcept {
    for (; size_ < other.size_; ++size_) {
      std::construct_at(const_cast<std::remove_const_t<T>*>(&values_[size_].data), other.values_[size_].data);
    }
  }

  constexpr LimitedVector& operator=(const LimitedVector& other) noexcept {
    if (this != &other) {
      clear();
      size_ = other.size_;
      values_ = other.values_;
    }
    return *this;
  }

  constexpr LimitedVector(LimitedVector&& other) noexcept {
    for (; size_ < other.size_; ++size_) {
      std::construct_at(
          const_cast<std::remove_const_t<T>*>(&values_[size_].data), std::move(other.values_[size_].data));
    }
    other.size_ = 0;
  }

  constexpr LimitedVector& operator=(LimitedVector&& other) noexcept {
    clear();
    for (; size_ < other.size_; ++size_) {
      std::construct_at(
          const_cast<std::remove_const_t<T>*>(&values_[size_].data), std::move(other.values_[size_].data));
    }
    other.size_ = 0;
    return *this;
  }

  // Constructors and assignment from other LimitVector/value types.

  template<std::forward_iterator It>
  requires types::ConstructibleFrom<T, mbo::types::ForwardIteratorValueType<It>>
  constexpr LimitedVector(It begin, It end) noexcept {
    while (begin < end) {
      emplace_back(*begin++);
    }
  }

  constexpr LimitedVector(const std::initializer_list<T>& list) noexcept {
    auto it = list.begin();
    while (it < list.end()) {
      emplace_back(*it++);
    }
  }

  template<types::ConstructibleInto<T> U>
  constexpr LimitedVector(const std::initializer_list<U>& list) noexcept {
    auto it = list.begin();
    while (it < list.end()) {
      emplace_back(*it++);
    }
  }

  template<types::ConstructibleInto<T> U, auto OtherN>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  constexpr LimitedVector& operator=(const std::initializer_list<U>& list) noexcept {
    assign(list);
    return *this;
  }

  template<types::ConstructibleInto<T> U, auto OtherN>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  constexpr explicit LimitedVector(const LimitedVector<U, OtherN>& other) noexcept {
    for (; size_ < other.size(); ++size_) {
      std::construct_at(const_cast<std::remove_const_t<T>*>(&values_[size_].data), other.at(size_));
    }
  }

  template<types::ConstructibleInto<T> U, auto OtherN>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  constexpr LimitedVector& operator=(const LimitedVector<U, OtherN>& other) noexcept {
    clear();
    for (; size_ < other.size(); ++size_) {
      std::construct_at(const_cast<std::remove_const_t<T>*>(&values_[size_].data), other.at(size_));
    }
    return *this;
  }

  template<types::ConstructibleInto<T> U, auto OtherN>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  // Moved element-wise below; `other` has a different type, so it cannot be moved as a whole.
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  constexpr explicit LimitedVector(LimitedVector<U, OtherN>&& other) noexcept {
    for (; size_ < other.size(); ++size_) {
      std::construct_at(const_cast<std::remove_const_t<T>*>(&values_[size_].data), std::move(other.at(size_)));
    }
    other.size_ = 0;
  }

  template<types::ConstructibleInto<T> U, auto OtherN>
  requires(MakeLimitedOptions<OtherN>().kCapacity <= Capacity)
  // Moved element-wise below; `other` has a different type, so it cannot be moved as a whole.
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  constexpr LimitedVector& operator=(LimitedVector<U, OtherN>&& other) noexcept {
    clear();
    for (; size_ < other.size(); ++size_) {
      std::construct_at(const_cast<std::remove_const_t<T>*>(&values_[size_].data), std::move(other.at(size_)));
    }
    other.size_ = 0;
    return *this;
  }

  // Modification: clear, resize, reserve, explace_back, push_back, pop_back, assign, insert

  constexpr void clear() noexcept {
    while (!empty()) {
      pop_back();
    }
  }

  constexpr void resize(std::size_t new_size) noexcept {
    while (new_size < size()) {
      pop_back();
    }
    while (new_size > size()) {
      push_back({});
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
  constexpr void swap(LimitedVector<U, OtherN>& other) noexcept {
    std::size_t pos = 0;
    for (; pos < size_ && pos < other.size(); ++pos) {
      std::swap(values_[pos].data, other.at(pos));
    }
    const std::size_t other_size = other.size_;
    const std::size_t this_size = size_;
    for (; pos < size_; ++pos) {
      other.emplace_back(std::move(values_[pos].data));
    }
    for (; pos < other.size(); ++pos) {
      emplace_back(std::move(other.at(pos)));
    }
    size_ = other_size;
    other.size_ = this_size;
  }

  template<typename... Args>
  constexpr iterator emplace(const_iterator pos, Args&&... args) noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(size_ < Capacity, "Called `emplace` at capacity.");
    MBO_CONFIG_REQUIRE(&values_[0].data <= pos && pos <= &values_[size_].data, "Invalid `pos`.");
    iterator dst = end();
    for (; dst > pos; --dst) {
      *dst = std::move(*std::prev(dst));
    }
    std::construct_at(const_cast<std::remove_const_t<T>*>(dst), std::forward<Args>(args)...);
    ++size_;
    return dst;
  }

  constexpr iterator erase(const_iterator pos) noexcept(!kRequireThrows) {
    // NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast)
    MBO_CONFIG_REQUIRE(&values_[0].data <= pos && pos < &values_[size_].data, "Invalid `pos`.");
    auto dst = const_cast<iterator>(pos);
    --size_;
    std::destroy_at(dst);
    for (; dst < end(); ++dst) {
      *dst = std::move(*std::next(dst));
    }
    return pos > end() ? end() : const_cast<iterator>(pos);
    // NOLINTEND(cppcoreguidelines-pro-type-const-cast)
  }

  constexpr iterator erase(const_iterator first, const_iterator last) noexcept(!kRequireThrows) {
    // NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast)
    MBO_CONFIG_REQUIRE(
        &values_[0].data <= first && first <= last && last <= &values_[size_].data, "Invalid `first` or `last`.");
    std::size_t deleted = 0;
    for (const_iterator it = first; it < last; ++it) {
      std::destroy_at(it);
      ++deleted;
    }
    auto dst = const_cast<iterator>(first);
    auto src = const_cast<iterator>(last);
    for (; src < end(); ++src, ++dst) {
      *dst = std::move(*src);
    }
    size_ -= deleted;
    return const_cast<iterator>(first);
    // NOLINTEND(cppcoreguidelines-pro-type-const-cast)
  }

  // Capacity exhaustion is a normal result, not a failed contract. No arguments
  // are consumed at capacity. Restrict construction to nothrow element types.
  template<typename... Args>
  requires std::is_nothrow_constructible_v<std::remove_const_t<T>, Args...>
  constexpr std::optional<std::reference_wrapper<T>> try_emplace_back(Args&&... args) noexcept {
    if (size_ == Capacity) {
      return std::nullopt;
    }
    return std::ref(emplace_back(std::forward<Args>(args)...));
  }

  template<typename... Args>
  constexpr reference emplace_back(Args&&... args) noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(size_ < Capacity, "Called `emplace_back` at capacity.");
    auto& data_ref{values_[size_++]};
    std::construct_at(const_cast<std::remove_const_t<T>*>(&data_ref.data), std::forward<Args>(args)...);
    return data_ref.data;
  }

  constexpr reference push_back(T&& val) noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(size_ < Capacity, "Called `push_back` at capacity.");
    auto& data_ref{values_[size_++]};
    std::construct_at(const_cast<std::remove_const_t<T>*>(&data_ref.data), std::move(val));
    return data_ref.data;
  }

  constexpr reference push_back(const T& val) noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(size_ < Capacity, "Called `push_back` at capacity.");
    auto& data_ref{values_[size_++]};
    std::construct_at(const_cast<std::remove_const_t<T>*>(&data_ref.data), val);
    return data_ref.data;
  }

  constexpr void pop_back() noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(size_ > 0, "No element to pop.");
    std::destroy_at(&values_[--size_].data);
  }

  constexpr void assign(std::size_t num, const T& value) noexcept {
    clear();
    for (; num > 0; --num) {
      push_back(value);
    }
  }

  template<std::forward_iterator It>
  constexpr void assign(It begin, It end) noexcept {
    clear();
    while (begin < end) {
      emplace_back(std::forward<mbo::types::ForwardIteratorValueType<It>>(*begin++));
    }
  }

  constexpr void assign(const std::initializer_list<T>& list) noexcept(!kRequireThrows) {
    MBO_CONFIG_REQUIRE(list.size() <= Capacity, "Called `assign` at capacity.");
    clear();
    auto it = list.begin();
    while (it < list.end()) {
      emplace_back(*it++);
    }
  }

  template<types::ConstructibleInto<T> U>
  constexpr iterator insert(const_iterator pos, U&& value) {
    MBO_CONFIG_REQUIRE(size_ < Capacity, "Called `insert` at capacity.");
    MBO_CONFIG_REQUIRE(begin() <= pos && pos <= end(), "Invalid `pos`.");
    // Clang does not like `std::distance`. The issue is that the iterators point into the union.
    // That makes them technically not point into an array AND that is indeed not allowed by C++.
    const auto dst = const_cast<iterator>(pos);  // NOLINT(cppcoreguidelines-pro-type-const-cast)
    move_backward(dst, 1);
    std::construct_at(const_cast<std::remove_const_t<T>*>(&*dst), std::forward<U>(value));
    return dst;
  }

  constexpr iterator insert(const_iterator pos, size_type count, const T& value) {
    MBO_CONFIG_REQUIRE(size_ + count <= Capacity, "Called `insert` at capacity.");
    MBO_CONFIG_REQUIRE(begin() <= pos && pos <= end(), "Invalid `pos`.");
    const auto dst = const_cast<iterator>(pos);  // NOLINT(cppcoreguidelines-pro-type-const-cast)
    if (count == 0) {
      return dst;
    }
    std::size_t index = move_backward(dst, count);
    while (count > 0) {
      std::construct_at(const_cast<std::remove_const_t<T>*>(&values_[index].data), value);
      ++index;
      --count;
    }
    return dst;
  }

  constexpr iterator insert(const_iterator pos, const T& value) { return insert(pos, 1, value); }

  template<typename InputIt>
  requires(types::ConstructibleFrom<T, decltype(*std::declval<InputIt>())>)
  constexpr iterator insert(const_iterator pos, InputIt first, InputIt last) {
    MBO_CONFIG_REQUIRE(begin() <= pos && pos <= end(), "Invalid `pos`.");
    MBO_CONFIG_REQUIRE(first <= last, "First > Last.");
    std::size_t count = std::distance(first, last);
    MBO_CONFIG_REQUIRE(size_ + count <= Capacity, "Called `insert` at capacity.");
    const auto dst = const_cast<iterator>(pos);  // NOLINT(*-pro-type-const-cast)
    if (count == 0) {
      return dst;
    }
    std::size_t index = move_backward(dst, count);
    while (count) {
      std::construct_at(const_cast<std::remove_const_t<T>*>(&values_[index].data), *first);
      ++index;
      --count;
      ++first;
    }
    return dst;
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

  constexpr iterator begin() noexcept { return &values_[0].data; }

  constexpr const_iterator begin() const noexcept { return &values_[0].data; }

  constexpr const_iterator cbegin() const noexcept { return &values_[0].data; }

  constexpr iterator end() noexcept { return &values_[size_].data; }

  constexpr const_iterator end() const noexcept { return &values_[size_].data; }

  constexpr const_iterator cend() const noexcept { return &values_[size_].data; }

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

  constexpr const_pointer data() const noexcept { return &values_[0].data; }

 private:
  constexpr std::size_t move_backward(iterator dst, std::size_t count) {
    std::size_t pos = size_;
    while (dst < &values_[pos].data) {
      --pos;
      std::construct_at(const_cast<std::remove_const_t<T>*>(&values_[pos + count].data), std::move(values_[pos].data));
    }
    size_ += count;
    return pos;
  }

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
constexpr inline auto operator<=>(const LimitedVector<LHS, LN>& lhs, const LimitedVector<RHS, RN>& rhs) noexcept {
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
constexpr inline bool operator==(const LimitedVector<LHS, LN>& lhs, const LimitedVector<RHS, RN>& rhs) noexcept {
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
constexpr inline bool operator<(const LimitedVector<LHS, LN>& lhs, const LimitedVector<RHS, RN>& rhs) noexcept {
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
inline constexpr auto MakeLimitedVector(It&& begin, It&& end) noexcept {
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
inline constexpr auto MakeLimitedVector(const T& value) noexcept {
  auto result = LimitedVector<T, LimitedOptions<N, Flags...>{}>();
  result.assign(N, value);
  return result;
}

template<typename... Args>
requires((types::NotInitializerList<Args> && !std::forward_iterator<Args> && !types::IsCharArray<Args>) && ...)
inline constexpr auto MakeLimitedVector(Args&&... args) noexcept {
  using T = std::common_type_t<Args...>;
  auto result = LimitedVector<T, sizeof...(Args)>();
  (result.emplace_back(std::forward<T>(args)), ...);
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
inline constexpr auto MakeLimitedVector(Args... args) noexcept {
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
